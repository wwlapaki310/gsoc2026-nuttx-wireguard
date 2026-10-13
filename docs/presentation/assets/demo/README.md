# Backup demo video

Put the Stack-chan backup video here, named exactly:

    assets/demo/backup-demo.mp4

That is all. The "Backup video" button then appears on the **Live Demo** slide
(both the standalone deck and the reveal.js edition). Until the file exists,
the slide shows nothing about a video.

- Format: MP4, H.264 video + AAC audio (plays in Chrome, Edge, Firefox, Safari
  without a plug-in). Keep it a few tens of MB; GitHub refuses files over 100 MB.
- It plays from the local file, so it works without an Internet connection.
  If you present from `index.html` on the laptop, keep this folder next to it
  (`docs/presentation/assets/`).
- The button is small, at the top right of the slide. Click it, then use the
  normal player controls (play, pause, seek bar, volume, full screen). Close with
  the "Close" button or Esc. It never starts by itself.
- Different file name or location: edit `<source src="assets/demo/backup-demo.mp4">`
  in the "Live Demo" slide of `../../coc-glasgow-slides.html`, then run
  `python coc-glasgow-reveal/build.py` (and `renumber.py` is not needed).
- Check: open the deck, go to the Live Demo slide; the button appears when the
  file loads. Push the file together with the rest, so GitHub Pages serves it too.
