(function bootBambuStudio(){
  var shell = document.querySelector('template[data-shell]');
  var assembledString = shell.innerHTML;

  document.querySelectorAll('template[data-screen]').forEach(function(screen){
    var marker = '<!--SCREEN:' + screen.getAttribute('data-screen') + '-->';
    assembledString = assembledString.split(marker).join(screen.innerHTML);
  });

  assembledString = assembledString.replace(/<!--SCREEN:[a-z0-9_-]+-->/gi, '');

  DC.register('SearchField', document.querySelector('template[data-component="SearchField"]'), SearchField);
  DC.register('main', assembledString, Main);

  var qp = Object.fromEntries(new URLSearchParams(window.location.search));
  DC.mount('main', document.getElementById('app'), {
    theme:qp.theme||'dark',
    density:qp.density||'comfortable',
    accent:qp.accent||'#22c55e',
    view:qp.view||'prepare'
  });
})();
