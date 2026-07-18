/*
 * Shared Bambu Studio shell logic, ported from
 * design-source/Bambu Studio.dc.html.
 */
window.SCREENS = window.SCREENS || [];
window.registerScreen = window.registerScreen || function(){};

class Main extends DCLogic {
  constructor(props){
    super(props);
    this.state = {
      view: (props && props.view) || 'prepare',
      theme: (props && props.theme) || 'dark',
      density: (props && props.density) || 'comfortable',
      accent: (props && props.accent) || '#22c55e',
      tool: 'move',
      processTab: 'Quality',
      supportOn: false,
      showControls: false,
      dialog: null,
      gcodeScheme: 'Line type',
      previewOpts: { travel:true, seams:false, retractions:false, wipe:false },
      layer: 124,
      speedMode: 'Standard',
      lamp: true,
      snacks: [],
      branch: 'main',
      showHistory: false,
      selectedCommit: 'a1f9c02',
      history: [
        {hash:'a1f9c02', full:'a1f9c02e8b34', message:'Set sparse infill density → 15%', icon:'tune', time:'2 min ago', author:'You', files:'process_settings.json', diff:[{label:'Sparse infill density', from:'10%', to:'15%'},{label:'Infill pattern', from:'Gyroid', to:'Grid'}]},
        {hash:'7d3e5b8', full:'7d3e5b8a1c90', message:'Rotate 3DBenchy 45° around Z', icon:'rotate_right', time:'5 min ago', author:'You', files:'3DBenchy.stl', diff:[{label:'Rotation Z', from:'0°', to:'45°'}]},
        {hash:'e0b41aa', full:'e0b41aa77d12', message:'Scale 3DBenchy 100% → 120%', icon:'open_in_full', time:'6 min ago', author:'You', files:'3DBenchy.stl', diff:[{label:'Scale', from:'100%', to:'120%'},{label:'Size Z', from:'48.0mm', to:'57.6mm'}]},
        {hash:'c92f7d1', full:'c92f7d10ab55', message:'Move 3DBenchy to plate center', icon:'open_with', time:'9 min ago', author:'You', files:'3DBenchy.stl', diff:[{label:'Position X', from:'96.0mm', to:'128.0mm'},{label:'Position Y', from:'110.0mm', to:'128.0mm'}]},
        {hash:'4b8a1f0', full:'4b8a1f0d3e77', message:'Add 3DBenchy.stl', icon:'add', time:'12 min ago', author:'You', files:'3DBenchy.stl, project.json', diff:[{label:'Objects', from:'0', to:'1'},{label:'Faces', from:'—', to:'15,842'}]},
        {hash:'0000000', full:'000000000000', message:'Initialize project repository', icon:'flag', time:'12 min ago', author:'Bambu Studio', files:'project.3mf', diff:[{label:'Repository', from:'—', to:'created'}]},
      ],
      projectCat: 'Model pictures',
      prefs: { autoArrange:true, autoCommit:true, autoSaveToast:false, bundleRepo:true, hints:false, telemetry:false },
      export: { contains:'', type:'All', vendor:'All', format:'.bbsflmt', selected:null },
    };
  }
  componentDidUpdate(prev){
    const p = this.props || {};
    const patch = {};
    if (p.theme && p.theme !== prev.theme) patch.theme = p.theme;
    if (p.density && p.density !== prev.density) patch.density = p.density;
    if (p.accent && p.accent !== prev.accent) patch.accent = p.accent;
    if (p.view && p.view !== prev.view) patch.view = p.view;
    if (Object.keys(patch).length) this.setState(patch);
  }
  hexToHsl(hex){
    let h = (hex||'').replace('#',''); if(h.length===3) h=h.split('').map(c=>c+c).join('');
    const r=parseInt(h.slice(0,2),16)/255, g=parseInt(h.slice(2,4),16)/255, b=parseInt(h.slice(4,6),16)/255;
    const mx=Math.max(r,g,b), mn=Math.min(r,g,b); let hu=0, s=0, l=(mx+mn)/2;
    if(mx!==mn){ const d=mx-mn; s=l>0.5?d/(2-mx-mn):d/(mx+mn);
      if(mx===r) hu=(g-b)/d+(g<b?6:0); else if(mx===g) hu=(b-r)/d+2; else hu=(r-g)/d+4; hu/=6; }
    return { h:Math.round(hu*360), s:Math.round(s*100), l:Math.round(l*100) };
  }
  accentVars(seed, theme){
    const c = this.hexToHsl(seed); const h = c.h; const s = Math.max(32, Math.min(92, c.s));
    const H = (l)=>`hsl(${h} ${s}% ${l}%)`; const Hs=(sat,l)=>`hsl(${h} ${sat}% ${l}%)`;
    if(theme==='dark'){
      return [`--md-primary:${H(76)}`,`--md-on-primary:${H(16)}`,`--md-primary-container:${Hs(Math.round(s*0.9),28)}`,
        `--md-on-primary-container:${H(90)}`,`--md-accent:${H(70)}`,`--md-inverse-primary:${H(38)}`,
        `--md-secondary-container:${Hs(Math.round(s*0.35),26)}`,`--md-on-secondary-container:${H(88)}`].join(';')+';';
    }
    return [`--md-primary:${H(36)}`,`--md-on-primary:hsl(0 0% 100%)`,`--md-primary-container:${Hs(Math.round(s*0.7),88)}`,
      `--md-on-primary-container:${H(12)}`,`--md-accent:${H(38)}`,`--md-inverse-primary:${H(76)}`,
      `--md-secondary-container:${Hs(Math.round(s*0.45),90)}`,`--md-on-secondary-container:${H(20)}`].join(';')+';';
  }
  go(v){ return ()=>this.setState({view:v, showControls:false, dialog:null}); }
  render_tabs(){
    const cur = this.state.view;
    const defs = [
      {id:'home', label:'Home', icon:'home'},
      {id:'prepare', label:'Prepare', icon:'view_in_ar'},
      {id:'preview', label:'Preview', icon:'layers'},
      {id:'device', label:'Device', icon:'cast'},
      {id:'multi', label:'Multi-device', icon:'devices'},
      {id:'project', label:'Project', icon:'folder'},
      {id:'calibration', label:'Calibration', icon:'tune'},
      {id:'filament', label:'Filament', icon:'palette'},
      {id:'settings', label:'Settings', icon:'settings'},
    ];
    return defs.map(d=>{ const on = cur===d.id; return {
      ...d, onClick:this.go(d.id),
      fg: on?'var(--md-primary)':'var(--md-on-surface-variant)',
      weight: on?'600':'400', fill: on?'1':'0',
      indicator: on?'var(--md-primary)':'transparent',
    };});
  }
  render_filaments(){
    return [
      {color:'#111418', name:'Bambu PLA Basic', type:'PLA', slot:'AMS · Slot 1'},
      {color:'#e11d2e', name:'Bambu PLA Matte', type:'PLA', slot:'AMS · Slot 2'},
      {color:'#1560d4', name:'Bambu PETG HF', type:'PETG', slot:'AMS · Slot 3'},
      {color:'#f5c518', name:'Bambu Support', type:'SUP', slot:'AMS · Slot 4'},
    ];
  }
  render_accents(){
    const cur = (this.state.accent||'').toLowerCase();
    const list = [
      {name:'Green', color:'#22c55e'},{name:'Violet', color:'#7c5cff'},{name:'Teal', color:'#14b8a6'},
      {name:'Blue', color:'#3b82f6'},{name:'Orange', color:'#f97316'},{name:'Pink', color:'#ec4899'},
    ];
    return list.map(a=>{ const on = cur===a.color.toLowerCase(); return {
      ...a, onClick:()=>this.setState({accent:a.color}),
      border: on?'3px solid var(--md-on-surface)':'2px solid transparent',
      check: on?'1':'0',
    };});
  }
  commit(message, icon){
    const hash = Math.random().toString(16).slice(2,9);
    const entry = {hash, full:hash+Math.random().toString(16).slice(2,7), message, icon:icon||'edit', time:'just now', author:'You', files:'project.3mf', diff:[{label:'Action', from:'—', to:message}]};
    if(this.state.prefs && this.state.prefs.autoCommit===false) return;
    this.setState(st=>({ history:[entry, ...st.history], selectedCommit:hash }));
    if(this.state.prefs && this.state.prefs.autoSaveToast) this.notify('Auto-saved: '+message, {icon:'commit', duration:2600});
  }
  notify(message, opts){
    opts=opts||{}; const id=Date.now()+Math.random();
    this.setState(st=>({ snacks:[...st.snacks, {id, message, icon:opts.icon||'check_circle', action:opts.action||null, actionLabel:opts.actionLabel||''}] }));
    setTimeout(()=>this.dismissSnack(id), opts.duration||3200);
  }
  dismissSnack(id){ this.setState(st=>({ snacks: st.snacks.filter(x=>x.id!==id) })); }
  exMatch(name){ const q=this.state.export.contains; if(!q) return true; try{ return new RegExp(q,'i').test(name); }catch(e){ return name.toLowerCase().includes(q.toLowerCase()); } }
  toggleExportItem(name){ this.setState(st=>{ const sel={...(st.export.selected||{})}; sel[name]=!sel[name]; return {export:{...st.export, selected:sel}}; }); }
  toggleSelectAllExport(){ const ex=this.state.export; const rows=this.render_filRows().filter(f=>this.exMatch(f.name)&&(ex.type==='All'||f.type===ex.type)&&(ex.vendor==='All'||f.vendor===ex.vendor)); const sel={...(ex.selected||{})}; const all=rows.length>0&&rows.every(f=>sel[f.name]); rows.forEach(f=>sel[f.name]=!all); this.setState({export:{...ex,selected:sel}}); }
  doExport(){ const ex=this.state.export; const sel=ex.selected||{}; const n=Object.keys(sel).filter(k=>sel[k]).length; this.setState({dialog:null}); this.notify('Exported '+n+' filament preset'+(n===1?'':'s')+' \u2192 '+ex.format, {icon:'file_download', actionLabel:'Show file', action:this.go('project'), duration:4200}); }
  saveProject(){ this.notify('Project saved — version history bundled into .3mf', {icon:'save', duration:3800}); }
  render_filRows(){
    return [
      {name:'Bambu PLA Basic', vendor:'Bambu Lab', type:'PLA', nozzle:'220', bed:'55', color:'#111418'},
      {name:'Bambu PLA Matte', vendor:'Bambu Lab', type:'PLA', nozzle:'210', bed:'55', color:'#e11d2e'},
      {name:'Bambu PETG HF', vendor:'Bambu Lab', type:'PETG', nozzle:'255', bed:'70', color:'#1560d4'},
      {name:'Bambu ABS', vendor:'Bambu Lab', type:'ABS', nozzle:'270', bed:'90', color:'#f5c518'},
      {name:'Generic TPU', vendor:'Generic', type:'TPU', nozzle:'230', bed:'35', color:'#16a34a'},
      {name:'PolyTerra PLA', vendor:'Polymaker', type:'PLA', nozzle:'215', bed:'55', color:'#7c5cff'},
    ];
  }
  seg(active){ return { bg: active?'var(--md-primary)':'transparent', fg: active?'var(--md-on-primary)':'var(--md-on-surface-variant)' }; }

  commonVals(){
    const s = this.state;
    const ex = s.export; const exSel = ex.selected || {};
    const exFiltered = this.render_filRows().filter(f => this.exMatch(f.name) && (ex.type==='All'||f.type===ex.type) && (ex.vendor==='All'||f.vendor===ex.vendor));
    const exRows = exFiltered.map(f=>({ ...f, onToggle:()=>this.toggleExportItem(f.name), checkIcon: exSel[f.name]?'check_box':'check_box_outline_blank', checkColor: exSel[f.name]?'var(--md-primary)':'var(--md-on-surface-variant)', rowBg: exSel[f.name]?'var(--md-secondary-container)':'transparent' }));
    const exSelCount = exFiltered.filter(f=>exSel[f.name]).length;
    const exAllIcon = (exFiltered.length>0 && exSelCount===exFiltered.length) ? 'check_box' : (exSelCount>0 ? 'indeterminate_check_box' : 'check_box_outline_blank');
    const l = this.seg(s.theme==='light'), d = this.seg(s.theme==='dark');
    const cf = this.seg(s.density==='comfortable'), cp = this.seg(s.density==='compact');
    return {
      theme:s.theme, density:s.density,
      accentOverride:this.accentVars(s.accent, s.theme),
      projectName:'3DBenchy_project.3mf',
      tabs:this.render_tabs(),
      filaments:this.render_filaments(),
      menus:[
        {label:'File', onClick:()=>this.saveProject()},
        {label:'Edit', onClick:()=>this.setState({showHistory:true})},
        {label:'View', onClick:()=>this.notify('View menu')},
        {label:'Objects', onClick:()=>this.notify('Objects menu')},
        {label:'Help', onClick:()=>this.notify('Bambu Studio — Material Design 3 · v2.0.0', {icon:'info', actionLabel:'Docs', action:()=>{}})},
      ],
      showControls:s.showControls,
      toggleControls:()=>this.setState({showControls:!s.showControls}),
      stop:(e)=>{ if(e&&e.stopPropagation) e.stopPropagation(); },
      setLight:()=>this.setState({theme:'light'}), setDark:()=>this.setState({theme:'dark'}),
      setComfortable:()=>this.setState({density:'comfortable'}), setCompact:()=>this.setState({density:'compact'}),
      onPickAccent:(e)=>this.setState({accent:e.target.value}),
      accentSwatches:this.render_accents(),
      lightBg:l.bg, lightFg:l.fg, darkBg:d.bg, darkFg:d.fg,
      comfBg:cf.bg, comfFg:cf.fg, compBg:cp.bg, compFg:cp.fg,
      openSlice:()=>{ this.notify('Slicing Plate 1…', {icon:'hourglass_top', duration:1400}); setTimeout(()=>{ this.commit('Slice Plate 1','deployed_code'); this.notify('Plate 1 sliced · 1h 24m · 23.4 g', {icon:'check_circle', actionLabel:'Preview', action:this.go('preview'), duration:4200}); }, 1500); },
      openSend:()=>this.setState({dialog:'send'}),
      openAddFilament:()=>this.setState({dialog:'addfil'}),
      openDialog:(id)=>this.setState({dialog:id}), closeDialog:()=>this.setState({dialog:null}),
      goPrepare:this.go('prepare'), goDevice:this.go('device'), goHome:this.go('home'), goProject:this.go('project'),
      filamentRows:this.render_filRows(), historyCount:s.history.length,
      isDlgSend:s.dialog==='send', isDlgAbout:s.dialog==='about', isDlgAddfil:s.dialog==='addfil', isDlgSlice:s.dialog==='slice',
      openAbout:()=>this.setState({dialog:'about'}),
      snacks: s.snacks.map(n=>({ ...n, hasAction: !!n.action, dismiss:()=>this.dismissSnack(n.id), action: n.action||(()=>{}) })),
      branch: s.branch, historyHead: (s.history[0]||{}).hash,
      showHistory: s.showHistory, toggleHistory:()=>this.setState({showHistory:!s.showHistory}),
      autoCommitNote: 'Every edit is auto-committed to this project\u2019s local Git repo',
      history: s.history.map((h,i)=>{ const on=s.selectedCommit===h.hash; return { ...h, isHead:i===0, expanded:on, chevron:on?'expand_less':'expand_more', dotBg:on?'var(--md-primary)':'var(--md-sc-highest)', dotFg:on?'var(--md-on-primary)':'var(--md-on-surface-variant)', onSelect:()=>this.setState({selectedCommit: on?null:h.hash}), restore:()=>this.notify('Restored project to #'+h.hash, {icon:'history'}) }; }),
      addFilamentConfirm:()=>{ this.setState({dialog:null}); this.commit('Add filament: Bambu PLA Basic','palette'); },
      sendPrint:()=>{ this.setState({dialog:null}); this.notify('Sent to Bambu Lab X1 Carbon · print starting', {icon:'send', actionLabel:'Device', action:this.go('device'), duration:4200}); },
      exportFilament:()=>this.notify('Exported \u201CBambu PLA Basic\u201D \u2192 filament preset (.bbsflmt)', {icon:'file_download', actionLabel:'Show file', action:this.go('project'), duration:4200}),
      exportAllFilaments:()=>this.notify('Exported 6 filament presets \u2192 bundle (.bbsflmt.zip)', {icon:'folder_zip', duration:4000}),
      openExport:()=>{ const all={}; this.render_filRows().forEach(f=>all[f.name]=true); this.setState({ dialog:'export', export:{...this.state.export, selected:all, contains:'', type:'All', vendor:'All'} }); },
      isDlgExport: s.dialog==='export', setExQuery:(v)=>this.setState({export:{...this.state.export, contains:v}}),
      exRows, exSelCount, exTotal: exFiltered.length, exAllIcon, exFormat: s.export.format,
      exEmpty: exFiltered.length===0, exploreModels:()=>this.notify('Opening online model library…', {icon:'public'}),
      toggleSelectAllExport:()=>this.toggleSelectAllExport(), doExport:()=>this.doExport(),
      exTypeChips: ['All','PLA','PETG','ABS','TPU'].map(t=>{const on=s.export.type===t;return{label:t,onClick:()=>this.setState({export:{...s.export,type:t}}),bg:on?'var(--md-primary)':'transparent',fg:on?'var(--md-on-primary)':'var(--md-on-surface-variant)',border:on?'1px solid var(--md-primary)':'1px solid var(--md-outline)'};}),
      exVendorChips: ['All','Bambu Lab','Generic','Polymaker'].map(t=>{const on=s.export.vendor===t;return{label:t,onClick:()=>this.setState({export:{...s.export,vendor:t}}),bg:on?'var(--md-secondary-container)':'transparent',fg:on?'var(--md-on-secondary-container)':'var(--md-on-surface-variant)',border:on?'1px solid var(--md-primary)':'1px solid var(--md-outline-variant)'};}),
      exFormatChips: ['.bbsflmt','JSON','ZIP bundle'].map(t=>{const on=s.export.format===t;return{label:t,onClick:()=>this.setState({export:{...s.export,format:t}}),bg:on?'var(--md-primary)':'transparent',fg:on?'var(--md-on-primary)':'var(--md-on-surface-variant)',border:on?'1px solid var(--md-primary)':'1px solid var(--md-outline)'};}),
      dialog:s.dialog,
    };
  }

  renderVals(){ return Object.assign({}, this.commonVals(), ...(window.SCREENS||[]).map(s => s.vals.call(this))); }
}

window.Main=Main;
