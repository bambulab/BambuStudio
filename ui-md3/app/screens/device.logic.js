registerScreen({
  id: 'device',
  mixin: {
  render_temps(){
    return [
      {label:'Nozzle', val:'220', target:'220', icon:'mode_heat'},
      {label:'Bed', val:'60', target:'60', icon:'iron'},
      {label:'Chamber', val:'38', target:'--', icon:'thermostat'},
    ];
  },
  render_speedModes(){
    const cur=this.state.speedMode;
    return ['Silent','Standard','Sport','Ludicrous'].map(l=>{const on=cur===l;return{
      label:l, onClick:()=>this.setState({speedMode:l}),
      bg:on?'var(--md-primary)':'transparent', fg:on?'var(--md-on-primary)':'var(--md-on-surface-variant)'};});
  }
  },
  vals: function(){ const s = this.state; return {
    isDevice:this.state.view === 'device',
    temps:this.render_temps(), speedModes:this.render_speedModes(),
    lamp:s.lamp, toggleLamp:()=>this.setState({lamp:!s.lamp}),
    lampTrack:s.lamp?'var(--md-primary)':'transparent', lampTrackBorder:s.lamp?'var(--md-primary)':'var(--md-outline)',
    lampKnob:s.lamp?'var(--md-on-primary)':'var(--md-outline)', lampKnobX:s.lamp?'22px':'4px',
  }; }
});
