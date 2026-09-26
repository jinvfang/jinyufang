'use strict';

const menuButton = document.querySelector('.menu-toggle');
const navLinks = document.querySelector('.nav-links');
if (menuButton && navLinks) {
  menuButton.addEventListener('click', () => {
    const open = navLinks.classList.toggle('open');
    menuButton.setAttribute('aria-expanded', String(open));
  });
  navLinks.querySelectorAll('a').forEach((link) => link.addEventListener('click', () => {
    navLinks.classList.remove('open');
    menuButton.setAttribute('aria-expanded', 'false');
  }));
}

const filters = document.querySelectorAll('[data-filter]');
const cards = document.querySelectorAll('[data-category]');
filters.forEach((button) => button.addEventListener('click', () => {
  filters.forEach((item) => item.classList.remove('active'));
  button.classList.add('active');
  cards.forEach((card) => {
    card.hidden = button.dataset.filter !== 'all' && card.dataset.category !== button.dataset.filter;
  });
}));

const observer = 'IntersectionObserver' in window ? new IntersectionObserver((entries) => entries.forEach((entry) => {
  if (entry.isIntersecting) {
    entry.target.classList.add('visible');
    observer.unobserve(entry.target);
  }
}), { threshold: 0.12 }) : null;
document.querySelectorAll('.reveal').forEach((element) => observer ? observer.observe(element) : element.classList.add('visible'));

// Evidence photo viewer: click (or Enter on) any evidence image to see it full size.
const evidenceImages = [...document.querySelectorAll('.evidence-item img')];
if (evidenceImages.length) {
  const viewer = document.createElement('dialog');
  viewer.className = 'lightbox';
  viewer.setAttribute('aria-label', 'Enlarged photo');
  viewer.innerHTML = '<button class="lightbox-close" type="button" aria-label="Close">×</button>'
    + '<button class="lightbox-nav prev" type="button" aria-label="Previous photo">‹</button>'
    + '<figure><img alt=""><figcaption></figcaption></figure>'
    + '<button class="lightbox-nav next" type="button" aria-label="Next photo">›</button>';
  document.body.appendChild(viewer);
  const bigImage = viewer.querySelector('img');
  const bigCaption = viewer.querySelector('figcaption');
  let current = 0;

  const show = (index) => {
    current = (index + evidenceImages.length) % evidenceImages.length;
    const source = evidenceImages[current];
    bigImage.src = source.currentSrc || source.src;
    bigImage.alt = source.alt;
    bigCaption.innerHTML = source.closest('figure').querySelector('figcaption')?.innerHTML || '';
    if (!viewer.open) viewer.showModal();
  };

  evidenceImages.forEach((image, index) => {
    image.tabIndex = 0;
    image.setAttribute('role', 'button');
    image.setAttribute('aria-label', `Enlarge photo: ${image.alt}`);
    image.addEventListener('click', () => show(index));
    image.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); show(index); }
    });
  });

  viewer.querySelector('.lightbox-close').addEventListener('click', () => viewer.close());
  viewer.querySelector('.prev').addEventListener('click', () => show(current - 1));
  viewer.querySelector('.next').addEventListener('click', () => show(current + 1));
  viewer.addEventListener('click', (event) => { if (event.target === viewer) viewer.close(); });
  viewer.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowLeft') show(current - 1);
    if (event.key === 'ArrowRight') show(current + 1);
  });
  if (evidenceImages.length < 2) viewer.querySelectorAll('.lightbox-nav').forEach((button) => { button.hidden = true; });
}
