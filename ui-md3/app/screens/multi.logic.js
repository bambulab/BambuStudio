registerScreen({
  id: 'multi',
  mixin: {
    render_devices(){
      return [
        {name:'X1 Carbon', model:'Bambu Lab X1C', status:'Printing', pct:68, dot:'var(--md-primary)'},
        {name:'P1S — Studio', model:'Bambu Lab P1S', status:'Idle', pct:0, dot:'var(--md-outline)'},
        {name:'A1 mini', model:'Bambu Lab A1 mini', status:'Printing', pct:12, dot:'var(--md-primary)'},
        {name:'X1E — Lab', model:'Bambu Lab X1E', status:'Offline', pct:0, dot:'var(--md-error)'},
      ];
    }
  },
  vals: function(){ return {
    isMulti: this.state.view === 'multi',
    devices: this.render_devices()
  }; }
});
