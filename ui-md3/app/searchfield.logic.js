/*
 * SearchField logic ported from design-source/SearchField.dc.html.
 * The template itself is inlined in index.html so the component works on file://.
 */
class SearchField extends DCLogic {
  state = { value:'', pattern:'', open:false, regex:false, flags:{ i:true, w:false, m:false, s:false } };

  compileFlags(){
    const f=this.state.flags;
    return 'g'+(f.i?'i':'')+(f.m?'m':'')+(f.s?'s':'');
  }

  valid(){
    try{
      new RegExp(this.state.pattern, this.compileFlags());
      return true;
    }catch(e){
      return false;
    }
  }

  emit(v){
    if(typeof this.props.onQuery==='function') this.props.onQuery(v);
  }

  ins(t){
    // Compute once because mini-dc merges state synchronously before batching
    // its render; this preserves the design component's intended query value.
    const next=this.state.pattern+t;
    this.setState({ pattern:next, regex:true });
    this.emit(next);
  }

  flagChips(){
    const f=this.state.flags;
    const chip=(k,label)=>({
      label,
      onClick:()=>this.setState(st=>({ flags:{...st.flags, [k]:!st.flags[k]} })),
      bg:f[k]?'var(--md-primary)':'transparent',
      fg:f[k]?'var(--md-on-primary)':'var(--md-on-surface-variant)',
      border:f[k]?'1px solid var(--md-primary)':'1px solid var(--md-outline)'
    });
    return [
      chip('i','Ignore case'),
      chip('w','Whole word'),
      chip('m','Multiline'),
      chip('s','Dotall')
    ];
  }

  renderVals(){
    const s=this.state;
    const tokenDefs=[
      {t:'.',l:'any'},{t:'\\d',l:'digit'},{t:'\\w',l:'word'},{t:'\\s',l:'space'},{t:'^',l:'start'},{t:'$',l:'end'},
      {t:'\\b',l:'bound'},{t:'+',l:'1 or +'},{t:'*',l:'0 or +'},{t:'?',l:'optional'},{t:'()',l:'group'},{t:'[]',l:'set'},
      {t:'|',l:'or'},{t:'\\.',l:'literal .'},{t:'{2}',l:'repeat'},{t:'(?:)',l:'non-cap'},
    ];
    const ok=s.pattern==='' || this.valid();
    return {
      placeholder:this.props.placeholder || 'Search',
      value:s.value,
      pattern:s.pattern,
      open:s.open,
      hasValue:!!s.value,
      flagStr:this.compileFlags(),
      inputFont:s.regex ? "'Roboto Mono',monospace" : 'inherit',
      borderColor:s.open ? 'var(--md-primary)' : 'var(--md-outline)',
      regexBg:s.regex ? 'var(--md-primary)' : 'var(--md-sc-low)',
      regexFg:s.regex ? 'var(--md-on-primary)' : 'var(--md-on-surface-variant)',
      builderBg:s.open ? 'var(--md-secondary-container)' : 'var(--md-sc-low)',
      builderFg:s.open ? 'var(--md-on-secondary-container)' : 'var(--md-on-surface-variant)',
      validColor:ok ? 'var(--md-primary)' : 'var(--md-error)',
      validText:s.pattern==='' ? '' : (ok ? 'valid' : 'invalid'),
      onInput:(e)=>{
        this.setState({ value:e.target.value, pattern:s.regex ? e.target.value : s.pattern });
        this.emit(e.target.value);
      },
      onPattern:(e)=>{
        this.setState({ pattern:e.target.value, value:e.target.value, regex:true });
        this.emit(e.target.value);
      },
      clear:()=>{
        this.setState({ value:'', pattern:'' });
        this.emit('');
      },
      toggleRegex:()=>this.setState({ regex:!s.regex }),
      toggleBuilder:()=>this.setState({ open:!s.open }),
      applyBuilder:()=>{
        this.setState({ open:false, value:s.pattern, regex:true });
        this.emit(s.pattern);
      },
      clearBuilder:()=>this.setState({ pattern:'' }),
      tokens:tokenDefs.map(td=>({ ...td, onClick:()=>this.ins(td.t) })),
      flagChips:this.flagChips(),
    };
  }
}

window.SearchField=SearchField;
