#ifndef FIRSTFIT_HPP
#define FIRSTFIT_HPP

#include "selection_boilerplate.hpp"
// for writing SVG
//#include "../tools/svgtools.hpp"

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

#ifdef SVGTOOLS_HPP
        svg::SVGWriter<RawShape> svgwriter;
        std::for_each(first, last, [this,&svgwriter](Item &itm) { svgwriter.writeShape(itm, "none", "blue"); });
        svgwriter.save(boost::filesystem::path("SVG") / "all_items.svg");
#endif

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

        // debug: write down intitial order
        for (auto it = store_.begin(); it != store_.end(); ++it) {
            std::stringstream ss;
            ss << "initial order: " << it->get().name << ", p=" << it->get().priority() << ", bed_temp=" << it->get().bed_temp << ", height=" << it->get().height
               << ", area=" << it->get().area() << ", allowed_rotations=";
            for(auto r: it->get().allowed_rotations) ss << r << ", ";
            if (this->unfitindicator_)
                this->unfitindicator_(ss.str());
        }
        // debug: write down fixed items
        for (size_t i = 0; i < fixed_bins.size(); i++) {
            if (fixed_bins[i].empty())
                continue;
            std::stringstream ss;
            ss << "fixed bin " << i << ": ";
            for (auto it = fixed_bins[i].begin(); it != fixed_bins[i].end(); ++it) {
                ss << it->get().name << ", p=" << it->get().priority() << ", bed_temp=" << it->get().bed_temp << ", height=" << it->get().height
                   << ", area=" << it->get().area() << ", allowed_rotations=";
                for(auto r: it->get().allowed_rotations) ss << r << ", ";
                ss << ";   ";
            }
            if (this->unfitindicator_)
                this->unfitindicator_(ss.str());
        }

        int item_id = 0;
        auto makeProgress = [this, &item_id](Placer &placer, size_t bin_idx) {
            packed_bins_[bin_idx] = placer.getItems();
            this->last_packed_bin_id_ = int(bin_idx);
            this->progress_(static_cast<unsigned>(item_id));
        };

        auto& cancelled = this->stopcond_;

        //this->template remove_unpackable_items<Placer>(store_, bin, pconfig);

        for (auto it = store_.begin(); it != store_.end() && !cancelled(); ++it) {
            // skip unpackable item
            if (it->get().binId() == BIN_ID_UNFIT)
                continue;
            bool was_packed = false;
            int best_bed_id = -1;
            int bed_id_firstfit = -1;
            double score = LARGE_COST_TO_REJECT+1, best_score = LARGE_COST_TO_REJECT+1;
            double score_all_plates = 0, score_all_plates_best = std::numeric_limits<double>::max();
            typename Placer::PackResult result, result_best, result_firstfit;
            int j = 0;
            while (!was_packed && !cancelled() && placers.size() <= MAX_NUM_PLATES) {
                for(; j < placers.size() && !was_packed && !cancelled() && j<MAX_NUM_PLATES; j++) {
                    if (it->get().is_wipe_tower && it->get().binId() != placers[j].plateID()) {
                        if (this->unfitindicator_)
                            this->unfitindicator_(it->get().name + " cant be placed in plate_id=" + std::to_string(j) + "/" + std::to_string(placers.size()) + ", continue to next plate");
                        continue;
                    }
                    result = placers[j].pack(*it, rem(it, store_));
                    score = result.score();
                    score_all_plates = score + COST_OF_NEW_PLATE * j; // add a larger cost to larger plate id to encourace to use less plates
                    for (int i = 0; i < placers.size(); i++) { score_all_plates += placers[i].score();}
                    if (this->unfitindicator_)
                        this->unfitindicator_((boost::format("item %1% bed_id=%2%, score=%3%, score_all_plates=%4%, pos=(%5%, %6%)") % it->get().name % j % score %
                                               score_all_plates % unscale_(it->get().translation()[0]) % unscale_(it->get().translation()[1]))
                                                  .str());

                    if(score >= 0 && score < LARGE_COST_TO_REJECT) {
                        if (bed_id_firstfit == -1) {
                            bed_id_firstfit = j;
                            result_firstfit = result;
                        }
                        if (score_all_plates < score_all_plates_best) {
                            best_score = score;
                            score_all_plates_best = score_all_plates;
                            best_bed_id = j;
                            result_best = result;
                        }
                    }
                }
                if (best_bed_id == MAX_NUM_PLATES) {
                    // item is not fit because we have tried all possible plates to find a good enough fit
                    if (bed_id_firstfit == MAX_NUM_PLATES) {
                        it->get().binId(BIN_ID_UNFIT);
                        if (this->unfitindicator_)
                            this->unfitindicator_(it->get().name + " bed_id_firstfit == MAX_NUM_PLATES" + ",best_score=" + std::to_string(best_score));
                        break;
                    }
                    else {
                        // best bed is invalid, but firstfit bed is OK, use the latter
                        best_bed_id = bed_id_firstfit;
                        result_best = result_firstfit;
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

                // if the object can't be packed, try to pack it without extrusion calibration object
                bool placer_not_packed = !was_packed && j > 0 && j == placers.size() && placers[j - 1].getPackedSize() == 0; // large item is not placed into the bin
                if (placer_not_packed) {
                    if (it->get().has_tried_without_extrusion_cali_obj == false) {
                        it->get().has_tried_without_extrusion_cali_obj = true;
                        placers[j - 1].clearItems([](const Item &itm) { return itm.is_extrusion_cali_object; });
                        j = j - 1;
                        continue;
                    }
                }

                if(!was_packed){
                    if (this->unfitindicator_ && !placers.empty())
                        this->unfitindicator_(it->get().name + " not fit! plate_id=" + std::to_string(placers.back().plateID()) + ", score=" + std::to_string(score) +
                                              ", best_bed_id=" + std::to_string(best_bed_id) + ", score_all_plates=" + std::to_string(score_all_plates) +
                                              ", item.bed_id=" + std::to_string(it->get().binId()));
                    if (!placers.empty() && placers.back().getItems().empty()) {
                        it->get().binId(BIN_ID_UNFIT);
                        if (this->unfitindicator_) this->unfitindicator_(it->get().name + " can't fit into a new bin. Can't fit!");
                        // remove the last empty placer to force next item to be fit in existing plates first
                        if (placers.size() > 1) placers.pop_back();
                        break;
                    }
                    if (placers.size() < MAX_NUM_PLATES) {
                        placers.emplace_back(bin);
                        placers.back().plateID(placers.size() - 1);
                        placers.back().configure(pconfig);
                        if (fixed_bins.size() >= placers.size()) placers.back().preload(fixed_bins[placers.size() - 1]);
                        // placers.back().preload(pconfig.m_excluded_items);
                        packed_bins_.emplace_back();
                        j = placers.size() - 1;
                    } else {
                        it->get().binId(BIN_ID_UNFIT);
                        if (this->unfitindicator_) this->unfitindicator_(it->get().name + " can't fit into any bin. Can't fit!");
                        break;
                    }
                }
            }
        }
    }

};

}
}

#endif // FIRSTFIT_HPP
