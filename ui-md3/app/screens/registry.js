window.SCREENS = [];
window.registerScreen = function(def){
  if (def.mixin) Object.assign(Main.prototype, def.mixin);
  window.SCREENS.push(def);
};
