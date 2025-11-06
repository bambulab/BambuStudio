// 侧边导航：导轨 + 活动短竖条
(function($){
  function positionIndicator(){
    var $nav = $('#sideNav');
    var $active = $nav.find('.nav-item.active');
    if (!$active.length) return;
    var top = $active.position().top;
    var height = $active.outerHeight();
    $nav.find('.indicator').css({ top: top + 'px', height: height + 'px' });
  }

  function scrollToAnchor($item){
    var href = $item.find('a').attr('href');
    if (href && href.charAt(0) === '#'){
      var $target = $(href);
      if ($target.length){
        var offset = 70; // 顶部预留，避免顶到视口边缘
        $('html,body').animate({ scrollTop: Math.max(0, $target.offset().top - offset) }, 300);
      }
    }
  }

  $(function(){
    positionIndicator();
    $('#sideNav').on('click', '.nav-item', function(e){
      e.preventDefault();
      var $it = $(this);
      $it.addClass('active').siblings('.nav-item').removeClass('active');
      positionIndicator();
      scrollToAnchor($it);
    });

    // 滚动联动（ScrollSpy）
    var ticking = false;
    function computeActiveByScroll(){
      var scrollTop = $(window).scrollTop();
      var viewportOffset = 100; // 判定阈值，提前一点触发
      var $items = $('#sideNav .nav-item');
      var currentId = null;
      $items.each(function(){
        var href = $(this).find('a').attr('href');
        if (!href || href.charAt(0) !== '#') return;
        var $sec = $(href);
        if (!$sec.length) return;
        var top = $sec.offset().top;
        if (top <= scrollTop + viewportOffset){
          currentId = href;
        }
      });
      if (currentId){
        var $cur = $items.filter(function(){
          return $(this).find('a').attr('href') === currentId;
        }).first();
        if ($cur.length && !$cur.hasClass('active')){
          $cur.addClass('active').siblings('.nav-item').removeClass('active');
          positionIndicator();
        } else {
          // 位置可能变化，仍然校准指示条
          positionIndicator();
        }
      }
    }
    function onScroll(){
      if (!ticking){
        ticking = true;
        requestAnimationFrame(function(){
          computeActiveByScroll();
          ticking = false;
        });
      }
    }
    $(window).on('scroll', onScroll);
    $(window).on('resize', function(){
      positionIndicator();
      computeActiveByScroll();
    });

    // 初始根据当前滚动位置激活一次
    computeActiveByScroll();
  });
})(jQuery);
