registerScreen({
  id: 'home',
  mixin: {
  render_recent(){
    return [
      {name:'3DBenchy_project', meta:'Today · X1 Carbon'},{name:'Enclosure_v3', meta:'Yesterday · P1S'},
      {name:'Gridfinity_bins', meta:'2 days ago · A1 mini'},{name:'Phone_stand', meta:'Last week · X1C'},
    ];
  }
  },
  vals: function(){ return {
    isHome: this.state.view === 'home',
    recent:this.render_recent()
  }; }
});
