// Matching only: keep the original spelling in saved presets and filament IDs.
var FilamentVendorNames = {
    key: function(name) { return name.toLowerCase(); },
    same: function(left, right) { return this.key(left) === this.key(right); },
    unique: function(names) {
        var seen = new Set();
        return names.filter(function(name) {
            var key = FilamentVendorNames.key(name);
            if (seen.has(key)) return false;
            seen.add(key);
            return true;
        });
    }
};
