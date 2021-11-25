#ifndef FIRSTFIT_HPP
#define FIRSTFIT_HPP

#include "selection_boilerplate.hpp"

namespace libnest2d { namespace selections {

template<class RawShape>
class _FirstFitSelection: public SelectionBoilerplate<RawShape> {
    using Base = SelectionBoilerplate<RawShape>;
public:
    using typename Base::Item;
    using Config = int; //dummy

private:
    using Base::packed_bins_;
    using typename Base::ItemGroup;
    using Container = ItemGroup;//typename std::vector<_Item<RawShape>>;

    Container store_;

public:

    void configure(const Config& /*config*/) { }

    template<class TPlacer, class TIterator,
             class TBin = typename PlacementStrategyLike<TPlacer>::BinType,
             class PConfig = typename PlacementStrategyLike<TPlacer>::Config>
    void packItems(TIterator first,
                   TIterator last,
                   TBin&& bin,
                   PConfig&& pconfig = PConfig())
    {

        using Placer = PlacementStrategyLike<TPlacer>;

        store_.clear();
        store_.reserve(last-first);

        std::vector<Placer> placers;
        placers.reserve(last-first);
        
        typename Base::PackGroup fixed_bins;
        std::for_each(first, last, [this, &fixed_bins](Item& itm) {
            if (itm.isFixed()) {
                if (itm.binId() < 0) itm.binId(0);
                auto binidx = size_t(itm.binId());

                while (fixed_bins.size() <= binidx)
                    fixed_bins.emplace_back();

                fixed_bins[binidx].emplace_back(itm);
            }
            else {
                store_.emplace_back(itm);
            }
            });

        std::for_each(pconfig.m_excluded_regions.begin(), pconfig.m_excluded_regions.end(), [this, &pconfig](Item& itm) {
            pconfig.m_excluded_items.emplace_back(itm);
            });

        // If the packed_items array is not empty we have to create as many
        // placers as there are elements in packed bins and preload each item
        // into the appropriate placer
        //for(ItemGroup& ig : packed_bins_) {
        //    placers.emplace_back(bin);
        //    placers.back().configure(pconfig);
        //    placers.back().preload(ig);
        //}
        
        std::function<bool(Item& i1, Item& i2)> sortfunc;
        if (pconfig.sortfunc)
            sortfunc = pconfig.sortfunc;
        else {
            sortfunc = [](Item& i1, Item& i2) {
                int p1 = i1.priority(), p2 = i2.priority();
                if (p1 != p2)
                    return p1 > p2;

                return i1.bed_temp != i2.bed_temp ? (i1.bed_temp > i2.bed_temp) :
                        (i1.height != i2.height ? (i1.height < i2.height) : (i1.area() > i2.area()));
            };
        }

        std::sort(store_.begin(), store_.end(), sortfunc);

        auto total = last-first;
        auto makeProgress = [this, &total](Placer& placer, size_t bin_idx) {
            packed_bins_[bin_idx] = placer.getItems();
            this->last_packed_bin_id_ = int(bin_idx);
            this->progress_(static_cast<unsigned>(--total));
        };

        auto& cancelled = this->stopcond_;
        
        this->template remove_unpackable_items<Placer>(store_, bin, pconfig);

        int item_id = 0;
        for (auto it = store_.begin(); it != store_.end() && !cancelled(); ++it) {
            // skip unpackable item
            if (it->get().binId() == BIN_ID_UNSET)
                continue;
            bool was_packed = false;
            int best_bed_id = -1;
            double score, best_score = LARGE_COST_TO_REJECT;
            double score_all_plates = 0, score_all_plates_best = std::numeric_limits<double>::max();
            typename Placer::PackResult result, result_best;
            size_t j = 0;
            while(!was_packed && !cancelled()) {
                for(; j < placers.size() && !was_packed && !cancelled(); j++) {
                    result = placers[j].pack(*it, rem(it, store_));
                    score = result.score();
                    score_all_plates = std::accumulate(placers.begin(), placers.begin() + j, 0.0,
                        [](double sum, const Placer& elem) { return sum + elem.score(); });
                    if(score > 0 && score < LARGE_COST_TO_REJECT && score_all_plates<score_all_plates_best) {
                        best_score = score;
                        score_all_plates_best = score_all_plates;
                        best_bed_id = j;
                        result_best = result;
                    }
                }

                if(best_bed_id>=0)
                {
                    was_packed = true;
                    j = best_bed_id;
                    it->get().binId(int(j));
                    it->get().itemId(item_id++);
                    placers[j].accept(result_best);
                    makeProgress(placers[j], j);
                }

                if(!was_packed) {
                    placers.emplace_back(bin);
                    placers.back().configure(pconfig);
                    if (fixed_bins.size() >= placers.size())
                        placers.back().preload(fixed_bins[placers.size() - 1]);
                    placers.back().preload(pconfig.m_excluded_items);
                    packed_bins_.emplace_back();
                    j = placers.size() - 1;
                }
            }
        }
    }

};

}
}

#endif // FIRSTFIT_HPP
