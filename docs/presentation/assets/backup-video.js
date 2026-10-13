/*
 * Backup demo video for the "Live Demo" slide.
 *
 * The slide carries a hidden block (.bv) with a "Backup video" button and a
 * full-slide player (.bv-overlay, a plain HTML5 <video controls>). This script
 * shows the button only when the video file can really be loaded, so until
 * assets/demo/backup-demo.mp4 exists, the slide looks as if there were no
 * video at all: no empty player, no dead link. The file is read from disk
 * (or the web server) only; nothing needs the Internet.
 *
 *   open  : click the button          (the video does not start by itself)
 *   close : "Close" button or Esc     (playback is paused)
 *
 * While the player is open, keys stay in the player and do not move the
 * slides.
 */
(function () {
  function setup(bv) {
    var open = bv.querySelector(".bv-open");
    var overlay = bv.querySelector(".bv-overlay");
    var close = bv.querySelector(".bv-close");
    var video = bv.querySelector("video");
    var source = bv.querySelector("source");
    if (!open || !overlay || !close || !video || !source) { return; }

    function hide() {
      video.pause();
      overlay.hidden = true;
      open.focus();
    }

    open.addEventListener("click", function () {
      overlay.hidden = false;
      video.focus();
    });
    close.addEventListener("click", hide);
    overlay.addEventListener("keydown", function (event) {
      event.stopPropagation();           // keep arrow keys and space in the player
      if (event.key === "Escape") { hide(); }
    });

    // Probe the file with a throw-away element that reads only the metadata.
    var probe = document.createElement("video");
    probe.preload = "metadata";
    probe.muted = true;
    probe.addEventListener("loadedmetadata", function () { bv.hidden = false; });
    probe.src = source.src;
  }

  function init() {
    Array.prototype.forEach.call(document.querySelectorAll(".bv"), setup);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
