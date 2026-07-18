registerScreen({
  id: 'preview',
  mixin: {
    render_legend(){
      return [
        {color:'#ef6c3e', label:'Outer wall'},{color:'#2ea043', label:'Inner wall'},
        {color:'#b9772e', label:'Sparse infill'},{color:'#e3b341', label:'Internal solid infill'},
        {color:'#d2477e', label:'Top surface'},{color:'#1f9ed1', label:'Bridge'},
        {color:'#34c6c6', label:'Support'},{color:'#7c5cff', label:'Support interface'},
        {color:'#9aa0a6', label:'Gap infill'},{color:'#8b949e', label:'Prime tower'},
        {color:'#6e7681', label:'Travel'},
      ];
    },
    render_schemes(){
      const cur=this.state.gcodeScheme;
      return ['Line type','Speed','Layer time','Flow','Temperature'].map(l=>{const on=cur===l;return{
        label:l, onClick:()=>this.setState({gcodeScheme:l}),
        bg:on?'var(--md-secondary-container)':'transparent', fg:on?'var(--md-on-secondary-container)':'var(--md-on-surface-variant)',
        border:on?'1px solid var(--md-primary)':'1px solid var(--md-outline-variant)'};});
    },
    render_prevOpts(){
      const o=this.state.previewOpts;
      const defs=[{id:'travel',label:'Travel',icon:'near_me'},{id:'seams',label:'Seams',icon:'adjust'},{id:'retractions',label:'Retractions',icon:'undo'},{id:'wipe',label:'Wipe',icon:'cleaning_services'}];
      return defs.map(d=>{const on=o[d.id];return{...d,onClick:()=>this.setState({previewOpts:{...o,[d.id]:!on}}),
        bg:on?'var(--md-primary)':'transparent', fg:on?'var(--md-on-primary)':'var(--md-on-surface-variant)',
        border:on?'1px solid var(--md-primary)':'1px solid var(--md-outline)'};});
    }
  },
  vals: function(){
    const s = this.state;
    return {
      isPreview: this.state.view === 'preview',
      gcodeLegend:this.render_legend(), gcodeSchemes:this.render_schemes(), gcodeScheme:s.gcodeScheme,
      prevOpts:this.render_prevOpts(), layer:s.layer, maxLayer:180,
      onLayer:(e)=>this.setState({layer:parseInt(e.target.value,10)}), layerZ:(s.layer*0.2).toFixed(2),
    };
  }
});
