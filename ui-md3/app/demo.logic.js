/*
 * Framework acceptance demo. Each control deliberately causes new renderVals
 * closures and parent patches so identity/listener bugs are easy to spot.
 */
class Demo extends DCLogic {
  constructor(props){
    super(props);
    this.renderPasses=0;
    this.state={
      theme:'dark',
      density:'comfortable',
      accent:'#22c55e',
      parentTicks:0,
      query:'',
      queryEvents:0,
      nativeText:'',
      inputCalls:0,
      clickCalls:0,
      hoverClicks:0,
      lastEventType:'none yet',
      stressSteps:0,
      stressRunning:false,
      batchValue:0,
      groups:[
        {
          id:'geometry',
          name:'Geometry',
          items:[
            {id:'vertices', label:'Vertices', show:true},
            {id:'faces', label:'Faces', show:false},
          ]
        },
        {
          id:'toolpath',
          name:'Toolpath',
          items:[
            {id:'walls', label:'Walls', show:true},
            {id:'infill', label:'Infill', show:false},
          ]
        },
      ],
    };
  }

  hexToHsl(hex){
    let h=(hex||'').replace('#','');
    if(h.length===3) h=h.split('').map(c=>c+c).join('');
    const r=parseInt(h.slice(0,2),16)/255;
    const g=parseInt(h.slice(2,4),16)/255;
    const b=parseInt(h.slice(4,6),16)/255;
    const mx=Math.max(r,g,b);
    const mn=Math.min(r,g,b);
    let hu=0;
    let s=0;
    const l=(mx+mn)/2;
    if(mx!==mn){
      const d=mx-mn;
      s=l>0.5 ? d/(2-mx-mn) : d/(mx+mn);
      if(mx===r) hu=(g-b)/d+(g<b?6:0);
      else if(mx===g) hu=(b-r)/d+2;
      else hu=(r-g)/d+4;
      hu/=6;
    }
    return { h:Math.round(hu*360), s:Math.round(s*100), l:Math.round(l*100) };
  }

  accentVars(seed,theme){
    const c=this.hexToHsl(seed);
    const h=c.h;
    const s=Math.max(32,Math.min(92,c.s));
    const H=(l)=>'hsl('+h+' '+s+'% '+l+'%)';
    const Hs=(sat,l)=>'hsl('+h+' '+sat+'% '+l+'%)';
    if(theme==='dark'){
      return [
        '--md-primary:'+H(76),
        '--md-on-primary:'+H(16),
        '--md-primary-container:'+Hs(Math.round(s*0.9),28),
        '--md-on-primary-container:'+H(90),
        '--md-accent:'+H(70),
        '--md-inverse-primary:'+H(38),
        '--md-secondary-container:'+Hs(Math.round(s*0.35),26),
        '--md-on-secondary-container:'+H(88)
      ].join(';')+';';
    }
    return [
      '--md-primary:'+H(36),
      '--md-on-primary:hsl(0 0% 100%)',
      '--md-primary-container:'+Hs(Math.round(s*0.7),88),
      '--md-on-primary-container:'+H(12),
      '--md-accent:'+H(38),
      '--md-inverse-primary:'+H(76),
      '--md-secondary-container:'+Hs(Math.round(s*0.45),90),
      '--md-on-secondary-container:'+H(20)
    ].join(';')+';';
  }

  onQuery(value){
    this.setState(st=>({
      query:value,
      queryEvents:st.queryEvents+1,
      parentTicks:st.parentTicks+1,
      lastEventType:'input (from child onQuery)'
    }));
  }

  reverseNestedRows(event){
    this.setState(st=>({
      groups:[...st.groups].reverse().map(group=>({
        ...group,
        items:[...group.items].reverse()
      })),
      parentTicks:st.parentTicks+1,
      lastEventType:event.type
    }));
  }

  toggleConditionalRows(event){
    this.setState(st=>({
      groups:st.groups.map(group=>({
        ...group,
        items:group.items.map(item=>({ ...item, show:!item.show }))
      })),
      parentTicks:st.parentTicks+1,
      lastEventType:event.type
    }));
  }

  runBatchedUpdates(event){
    // Five synchronous updater calls merge immediately but schedule one patch.
    for(let i=0;i<5;i+=1){
      this.setState(st=>({
        batchValue:st.batchValue+1,
        parentTicks:st.parentTicks+1
      }));
    }
    this.setState({ lastEventType:event.type });
  }

  async runRenderStress(event){
    if(this.state.stressRunning) return;
    this.setState({ stressRunning:true, stressSteps:0, lastEventType:event.type });

    // Await between updates so these are 30 real patches, not one batched patch.
    for(let i=0;i<30;i+=1){
      await Promise.resolve();
      this.setState(st=>({
        stressSteps:st.stressSteps+1,
        parentTicks:st.parentTicks+1
      }));
    }
    await Promise.resolve();
    this.setState({ stressRunning:false });
  }

  renderVals(){
    this.renderPasses+=1;
    const s=this.state;
    return {
      theme:s.theme,
      density:s.density,
      accentOverride:this.accentVars(s.accent,s.theme),
      greeting:'Interpolation is live',
      renderPasses:this.renderPasses,
      parentTicks:s.parentTicks,
      meter:8+((this.renderPasses*7)%92),
      groups:s.groups,
      query:s.query || '∅',
      queryEvents:s.queryEvents,
      nativeText:s.nativeText,
      inputCalls:s.inputCalls,
      clickCalls:s.clickCalls,
      hoverClicks:s.hoverClicks,
      lastEventType:s.lastEventType,
      stressSteps:s.stressSteps,
      stressLabel:s.stressRunning ? 'Running…' : 'Run 30 real re-renders',
      batchValue:s.batchValue,

      onQuery:(value)=>this.onQuery(value),
      forceParent:(event)=>this.setState(st=>({
        parentTicks:st.parentTicks+1,
        lastEventType:event.type
      })),
      reverseNested:(event)=>this.reverseNestedRows(event),
      toggleConditional:(event)=>this.toggleConditionalRows(event),
      batchUpdates:(event)=>this.runBatchedUpdates(event),
      stressRenders:(event)=>this.runRenderStress(event),
      probeClick:(event)=>this.setState(st=>({
        clickCalls:st.clickCalls+1,
        parentTicks:st.parentTicks+1,
        lastEventType:event.type
      })),
      probeInput:(event)=>this.setState(st=>({
        nativeText:event.target.value,
        inputCalls:st.inputCalls+1,
        parentTicks:st.parentTicks+1,
        lastEventType:event.type
      })),
      hoverClick:(event)=>this.setState(st=>({
        hoverClicks:st.hoverClicks+1,
        parentTicks:st.parentTicks+1,
        lastEventType:event.type
      })),
    };
  }
}

window.Demo=Demo;
