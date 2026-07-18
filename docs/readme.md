# Docs

The documentation site is generated with Doxygen from the existing Markdown in this `docs/` tree together with comments from the source code. The Doxygen configuration lives at the repository root in [`Doxygen`](../Doxygen).

```bash
# install doxygen if it is not already available
sudo apt-get update && sudo apt-get install -y doxygen

# from the repository root
doxygen Doxygen
```

The generated HTML site is written to `docs/html/` and the entry page is `docs/html/index.html`.

For GitHub Pages, this repository can use GitHub Actions as both the build source and the deployment source. The workflow in `.github/workflows/doxygen-pages.yml` rebuilds the site whenever documentation or source files change and deploys the generated `docs/html/` artifact to Pages.