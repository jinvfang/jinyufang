# Jinyu Fang Portfolio

This is a static HTML, CSS, and JavaScript portfolio.

## Editing projects

Every project has its own standalone HTML file in `projects/`. Open the relevant file and edit the clearly labeled content inside `<main>`:

- The project category, title, role, and date are in the project header.
- The main photo is the `<img>` inside `.project-gallery`. Add more `<img>` elements there for additional photos.
- The overview text is inside `.case-copy`.
- The bullet points are inside `.outcomes`.

Project images live in `assets/images/`. After adding or replacing an image, update its `src` and descriptive `alt` text in both the project page and its card in `index.html`.

All pages share `assets/css/style.css`, so editing an individual project will not change the common visual design.

To add a new project, copy `projects/_template.html`, rename it, replace the placeholder content, and add a matching card link to `index.html`.
