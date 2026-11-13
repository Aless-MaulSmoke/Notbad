(function(){
  var links=[].slice.call(document.querySelectorAll('nav.tree a'));
  var tabs=[].slice.call(document.querySelectorAll('.tabs a'));
  var map={};links.forEach(function(a){map[a.getAttribute('href').slice(1)]=a});
  var heads=[].slice.call(document.querySelectorAll('main section[id], main h3[id]'));

  function setActive(id){
    links.forEach(function(a){a.classList.remove('on')});
    if(map[id])map[id].classList.add('on');
    // A aba ativa é a última cujo alvo vem antes (ou é) a seção atual, na ordem do documento
    var cur=document.getElementById(id),active=tabs[0];
    tabs.forEach(function(a){
      var t=document.getElementById(a.getAttribute('href').slice(1));
      if(t&&cur&&(t===cur||(t.compareDocumentPosition(cur)&Node.DOCUMENT_POSITION_FOLLOWING)))active=a;
    });
    tabs.forEach(function(a){
      if(a===active)a.setAttribute('aria-current','page');
      else a.removeAttribute('aria-current');
    });
  }
  // Seção atual = último título que já passou da barra fixa
  var bar0=document.querySelector('.topbar');
  function currentId(){
    if(window.innerHeight+window.scrollY>=document.documentElement.scrollHeight-2)return 'credits';
    var line=bar0.offsetHeight+40,id=heads[0].id;
    for(var k=0;k<heads.length;k++){
      if(heads[k].getBoundingClientRect().top<=line)id=heads[k].id;else break;
    }
    return id;
  }
  // Ao clicar, marca na hora e ignora a rolagem suave até ela terminar
  var locked=false,timer=null;
  function unlock(){locked=false;setActive(currentId())}
  function lock(){locked=true;clearTimeout(timer);timer=setTimeout(unlock,200)}
  function onScroll(){
    if(locked){clearTimeout(timer);timer=setTimeout(unlock,200);return}
    setActive(currentId());
  }
  window.addEventListener('scroll',onScroll,{passive:true});
  window.addEventListener('resize',onScroll);
  tabs.forEach(function(a){
    a.addEventListener('click',function(){
      tabs.forEach(function(x){x.removeAttribute('aria-current')});
      a.setAttribute('aria-current','page');
      lock();
    });
  });
  links.forEach(function(a){
    a.addEventListener('click',function(){setActive(a.getAttribute('href').slice(1));lock()});
  });
  setActive(currentId());

  // Altura da barra fixa (usada pelo menu, rolagem e âncoras)
  var bar=document.querySelector('.topbar');
  function setBarH(){document.documentElement.style.setProperty('--topbar-h',bar.offsetHeight+'px')}
  setBarH();
  window.addEventListener('resize',setBarH);
  window.addEventListener('load',setBarH);
  if('ResizeObserver' in window)new ResizeObserver(setBarH).observe(bar);

  // Menu hambúrguer (celular)
  var btn=document.getElementById('menuBtn'),nav=document.getElementById('toc');
  var mq=window.matchMedia('(max-width:760px)');
  function setMenu(open){
    nav.classList.toggle('open',open);
    document.body.classList.toggle('menu-open',open);
    btn.setAttribute('aria-expanded',open);
    btn.setAttribute('aria-label',open?'Close menu':'Open menu');
    if(open){var on=nav.querySelector('a.on');if(on)on.scrollIntoView({block:'center'})}
  }
  btn.addEventListener('click',function(){setMenu(!nav.classList.contains('open'))});
  nav.addEventListener('click',function(e){if(e.target.closest('a'))setMenu(false)});
  document.addEventListener('click',function(e){
    if(nav.classList.contains('open')&&!nav.contains(e.target)&&!btn.contains(e.target))setMenu(false);
  });
  document.addEventListener('keydown',function(e){
    if(e.key==='Escape'&&nav.classList.contains('open')){setMenu(false);btn.focus()}
  });
  var onResize=function(){if(!mq.matches)setMenu(false)};
  if(mq.addEventListener)mq.addEventListener('change',onResize);else mq.addListener(onResize);
})();