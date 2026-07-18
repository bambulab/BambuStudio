registerScreen({
  id: 'settings',
  mixin: {
  render_prefs(){
    const p=this.state.prefs;
    const defs=[
      {k:'autoArrange', label:'Auto-arrange on import', desc:'Automatically arrange new objects on the plate'},
      {k:'autoCommit', label:'Auto-commit every edit to Git', desc:'Save each undoable change to the project repository'},
      {k:'autoSaveToast', label:'Show “auto-saved” notifications', desc:'Pop a snackbar each time an edit is committed'},
      {k:'bundleRepo', label:'Bundle version history into .3mf', desc:'Include the local Git repo when saving a project'},
      {k:'hints', label:'Show daily tips', desc:'Display slicing tips on launch'},
      {k:'telemetry', label:'Share anonymous usage data', desc:'Help improve Bambu Studio'},
    ];
    return defs.map(d=>{ const on=p[d.k]; return { label:d.label, desc:d.desc,
      onClick:()=>this.setState(st=>({ prefs:{...st.prefs, [d.k]:!st.prefs[d.k]} })),
      track:on?'var(--md-primary)':'transparent', trackBorder:on?'var(--md-primary)':'var(--md-outline)',
      knob:on?'var(--md-on-primary)':'var(--md-outline)', knobX:on?'22px':'4px', knobSize:on?'16px':'12px' };});
  }
  },
  vals: function(){ return {
    isSettings: this.state.view === 'settings',
    prefs: this.render_prefs()
  }; }
});
