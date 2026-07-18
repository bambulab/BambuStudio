registerScreen({
  id: 'calibration',
  mixin: {
    render_cali(){
      return [
        {title:'Flow Dynamics', desc:'Calibrate pressure advance for sharp corners', icon:'water_drop'},
        {title:'Flow Rate', desc:'Tune extrusion multiplier for accurate walls', icon:'opacity'},
        {title:'Max Volumetric Speed', desc:'Find the fastest reliable flow for a filament', icon:'speed'},
        {title:'Temperature Tower', desc:'Find the ideal nozzle temperature', icon:'thermostat'},
        {title:'Retraction Test', desc:'Reduce stringing and oozing', icon:'undo'},
        {title:'Vertical Fine Tuning', desc:'Correct Z-offset and first layer', icon:'height'},
      ].map(c=>({ ...c, onClick:()=>this.notify('Starting '+c.title+' calibration…', {icon:c.icon}) }));
    }
  },
  vals: function(){ return {
    isCalibration:this.state.view === 'calibration',
    caliCards:this.render_cali()
  }; }
});
