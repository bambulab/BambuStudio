registerScreen({
  id: 'prepare',
  mixin: {
  render_gizmos(){
    const cur = this.state.tool;
    const defs = [
      {id:'move', icon:'open_with', label:'Move (M)'},
      {id:'rotate', icon:'rotate_right', label:'Rotate (R)'},
      {id:'scale', icon:'open_in_full', label:'Scale (S)'},
      {id:'place', icon:'vertical_align_bottom', label:'Place on face'},
      {id:'cut', icon:'content_cut', label:'Cut (C)'},
      {id:'boolean', icon:'join_inner', label:'Mesh Boolean'},
      {id:'measure', icon:'straighten', label:'Measure'},
      {id:'assembly', icon:'category', label:'Assembly'},
      {id:'support', icon:'account_tree', label:'Support painting'},
      {id:'seam', icon:'polyline', label:'Seam painting'},
      {id:'color', icon:'format_paint', label:'Color painting'},
      {id:'text', icon:'text_fields', label:'Text'},
      {id:'svg', icon:'shapes', label:'SVG'},
      {id:'brim', icon:'adjust', label:'Brim ears'},
      {id:'fuzzy', icon:'blur_on', label:'Fuzzy skin'},
      {id:'simplify', icon:'compress', label:'Simplify mesh'},
    ];
    const noEdit={measure:1, assembly:1};
    return defs.map(d=>{ const on = cur===d.id; const nm=d.label.replace(/ \(.\)$/,''); return {
      ...d, onClick:()=>{ this.setState({tool:d.id}); if(noEdit[d.id]) this.notify(nm+' tool'); else this.commit(nm, d.icon); },
      bg: on?'var(--md-primary)':'transparent',
      fg: on?'var(--md-on-primary)':'var(--md-on-surface-variant)',
      fill: on?'1':'0',
    };});
  },
  render_scene(){
    const defs = [
      {id:'add', icon:'add', label:'Add object'},
      {id:'addplate', icon:'add_box', label:'Add plate'},
      {id:'orient', icon:'screen_rotation_alt', label:'Auto orient'},
      {id:'arrange', icon:'grid_view', label:'Arrange all'},
      {id:'layers', icon:'stacked_bar_chart', label:'Variable layer height'},
      {id:'split', icon:'call_split', label:'Split to objects'},
      {id:'assembly', icon:'deployed_code', label:'Assembly view'},
      {id:'more', icon:'more_horiz', label:'More tools'},
    ];
    const act={add:['Add object','add'],addplate:['Add plate','add_box'],orient:['Auto-orient objects','screen_rotation_alt'],arrange:['Arrange plate','grid_view'],layers:['Edit variable layer height','stacked_bar_chart'],split:['Split to objects','call_split']};
    return defs.map(d=>({...d, onClick: act[d.id] ? ()=>this.commit(act[d.id][0], act[d.id][1]) : ()=>this.notify(d.label) }));
  },
  render_process_tabs(){
    const cur = this.state.processTab;
    return ['Quality','Strength','Speed','Support'].map(l=>{ const on = cur===l; return {
      label:l, onClick:()=>this.setState({processTab:l}),
      bg: on?'var(--md-sc-lowest)':'transparent',
      fg: on?'var(--md-on-surface)':'var(--md-on-surface-variant)',
    };});
  },
  render_params(){
    const sw = this.state.supportOn;
    return [
      {isValue:true, label:'Layer height', value:'0.20', unit:'mm'},
      {isValue:true, label:'Wall loops', value:'3', unit:''},
      {isValue:true, label:'Top shell layers', value:'5', unit:''},
      {isValue:true, label:'Sparse infill density', value:'15', unit:'%'},
      {isSelect:true, label:'Infill pattern', value:'Grid'},
      {isSwitch:true, label:'Enable support', onClick:()=>{ this.commit((sw?'Disable':'Enable')+' support','account_tree'); this.setState({supportOn:!sw}); },
        track: sw?'var(--md-primary)':'transparent', trackBorder: sw?'var(--md-primary)':'var(--md-outline)',
        knob: sw?'var(--md-on-primary)':'var(--md-outline)', knobX: sw?'22px':'4px', knobSize: sw?'16px':'12px'},
      {isSelect:true, label:'Brim type', value:'Auto'},
    ];
  },
  render_manip(){
    return [
      {label:'Position', x:'128', y:'128', z:'0'},
      {label:'Rotation', x:'0°', y:'0°', z:'0°'},
      {label:'Scale', x:'100%', y:'100%', z:'100%'},
      {label:'Size', x:'60.0', y:'31.0', z:'48.0'},
    ];
  }
  },
  vals: function(){ return {
    isPrepare: this.state.view === 'prepare',
    gizmos:this.render_gizmos(),
    sceneTools:this.render_scene(),
    processTabs:this.render_process_tabs(),
    params:this.render_params(),
    manipRows:this.render_manip(),
  }; }
});
