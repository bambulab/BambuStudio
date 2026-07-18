registerScreen({
  id: 'project',
  mixin: {
  render_projectCats(){
    const cur=this.state.projectCat||'Model pictures';
    return [
      {label:'Model pictures', icon:'image', count:3},{label:'Bill of materials', icon:'receipt_long', count:1},
      {label:'Assembly guide', icon:'menu_book', count:2},{label:'Others', icon:'folder', count:4},
    ].map(c=>{const on=cur===c.label;return{...c,onClick:()=>this.setState({projectCat:c.label}),
      bg:on?'var(--md-secondary-container)':'transparent', fg:on?'var(--md-on-secondary-container)':'var(--md-on-surface-variant)'};});
  },
  render_projectFiles(){
    return [
      {name:'hull_front.png', type:'PNG', icon:'image'},{name:'hull_rear.png', type:'PNG', icon:'image'},
      {name:'cover.png', type:'PNG', icon:'image'},{name:'assembly_1.pdf', type:'PDF', icon:'picture_as_pdf'},
      {name:'assembly_2.pdf', type:'PDF', icon:'picture_as_pdf'},{name:'notes.txt', type:'TXT', icon:'description'},
    ];
  }
  },
  vals: function(){ return {
    isProject: this.state.view === 'project',
    projectCats: this.render_projectCats(),
    projectFiles: this.render_projectFiles()
  }; }
});
