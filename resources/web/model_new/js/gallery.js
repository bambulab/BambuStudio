/*
 * Simple Gallery (jQuery 2.1.1)
 * 用法：
 *   $('#el').bsGallery({ images: ['a.jpg','b.jpg', ...] });
 * 可选项：
 *   initialIndex, loop, thumbWidth, thumbHeight, mainHeight, counter
 */
(function($){
  'use strict';

  var defaults = {
    images: [],            // 必填：图片数组（字符串或 {src, thumb}）
    initialIndex: 0,
    loop: true,
    thumbHeight: 100,
    mainHeight: 420,
    // 新增：主图宽度与缩略栏宽度（可用数字或带单位字符串，如 '600px'、'60%'}
    mainWidth: 560,
    thumbsWidth: 180,
    counter: true
  };

  function buildDom($root, settings){
    $root.addClass('bs-gallery');

    var $main = $('<div class="bs-gallery-main" />');
    var $img = $('<img class="bs-gallery-image" alt="" />');
    var $counter = $('<div class="bs-gallery-counter" />');
    var $thumbs = $('<div class="bs-gallery-thumbs" />');

    $main.append($img);
    if (settings.counter) $main.append($counter);

    $root.append($main).append($thumbs);

    // 尺寸
    $main.css({ height: settings.mainHeight + 'px' });
    $thumbs.css({ maxHeight: settings.mainHeight + 'px' });

    // 应用主图宽度与缩略栏宽度
    if (settings.mainWidth != null) {
      var mw = typeof settings.mainWidth === 'number' ? (settings.mainWidth + 'px') : settings.mainWidth;
      $main.css({ width: mw });
      // 固定主图宽度，避免被 flex 拉伸
      $main.css({ flex: '0 0 ' + mw });
    }
    if (settings.thumbsWidth != null) {
      var tw = typeof settings.thumbsWidth === 'number' ? (settings.thumbsWidth + 'px') : settings.thumbsWidth;
      $thumbs.css({ width: tw });
    }

    // 生成缩略图
    $.each(settings.images, function(i, it){
      var src = typeof it === 'string' ? it : it.src;
      var thumb = typeof it === 'string' ? it : (it.thumb || it.src);
      var $t = $('<div class="bs-gallery-thumb" />').attr('data-index', i);
      var $ti = $('<img />').attr('src', thumb).attr('alt','');
      $t.append($ti);
      $thumbs.append($t);
    });

    // 返回引用
    return { $main:$main, $img:$img, $counter:$counter, $thumbs:$thumbs };
  }

  function plugin($root, options){
    // 清理旧实例：移除事件、数据与DOM，支持重复初始化
    $root.off('.bsGallery');
    $root.removeData('bsGallery');
    $root.removeClass('bs-gallery');
    $root.empty();

    var settings = $.extend({}, defaults, options||{});
    if (!settings.images || !settings.images.length) return;

    var ui = buildDom($root, settings);
    var count = settings.images.length;
    var index = Math.max(0, Math.min(settings.initialIndex, count - 1));

    function srcAt(i){
      var it = settings.images[i];
      return typeof it === 'string' ? it : it.src;
    }

    function updateCounter(){
      if (!settings.counter) return;
      ui.$counter.text((index + 1) + '/' + count);
    }

    function setActiveThumb(){
      ui.$thumbs.children('.bs-gallery-thumb').removeClass('active');
      ui.$thumbs.children('.bs-gallery-thumb').eq(index).addClass('active');
      // 确保可见
      var $active = ui.$thumbs.children('.bs-gallery-thumb').eq(index);
      var cTop = ui.$thumbs.scrollTop();
      var cH = ui.$thumbs.height();
      var tTop = $active.position().top + cTop;
      var tH = $active.outerHeight();
      if (tTop < cTop) ui.$thumbs.scrollTop(tTop);
      else if (tTop + tH > cTop + cH) ui.$thumbs.scrollTop(tTop + tH - cH);
    }

    function show(i){
      if (i === index) return;
      index = i;
      ui.$img.stop(true, true).fadeOut(120, function(){
        ui.$img.attr('src', srcAt(index)).fadeIn(120);
      });
      updateCounter();
      setActiveThumb();
    }

    function next(){
      if (index < count - 1) show(index + 1);
      else if (settings.loop) show(0);
    }

    function prev(){
      if (index > 0) show(index - 1);
      else if (settings.loop) show(count - 1);
    }

    // 事件：缩略图点击
    ui.$thumbs.on('click', '.bs-gallery-thumb', function(){
      var i = parseInt($(this).attr('data-index'), 10);
      show(i);
    });

    // 主图点击下一张
    ui.$main.on('click', function(){ next(); });

    // 键盘左右切换（容器获取焦点时）
    $root.attr('tabindex', 0);
    $root.on('keydown.bsGallery', function(e){
      if (e.which === 37) { // left
        prev(); e.preventDefault();
      } else if (e.which === 39) { // right
        next(); e.preventDefault();
      }
    });

    // 初始化显示
    ui.$img.attr('src', srcAt(index));
    setActiveThumb();
    updateCounter();

    // 暴露简单 API
    $root.data('bsGallery', { next: next, prev: prev, show: show, index: function(){return index;}, count: function(){return count;} });
  }

  $.fn.bsGallery = function(options){
    return this.each(function(){ plugin($(this), options); });
  };

})(jQuery);
