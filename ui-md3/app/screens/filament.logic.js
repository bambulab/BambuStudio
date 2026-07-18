registerScreen({
  id: 'filament',
  mixin: {},
  vals: function(){ return {
    isFilament: this.state.view === 'filament'
  }; }
});
