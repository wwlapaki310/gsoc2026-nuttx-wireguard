/*
 * Click-to-play YouTube video inside a slide.
 *
 * Markup (see the "Version 1 demo" slide):
 *
 *   <figure class="still yt" data-yt="VIDEO_ID">
 *     <button class="yt-play" type="button"><img src="thumbnail.jpg" alt="..."><span class="yt-btn">...</span></button>
 *   </figure>
 *
 * - Opening the slide shows only the thumbnail. Nothing is loaded from YouTube.
 * - A click replaces the thumbnail with the YouTube player and starts it (the
 *   click is the viewer's choice to play). Play, pause, volume, seek and full
 *   screen are YouTube's own controls.
 * - When the slide is left, the player is removed and the thumbnail is back,
 *   so the sound stops.
 * - Keys pressed in the player stay in the player (it is an iframe). Click
 *   outside it to move the slides with the keys again. The thumbnail button
 *   does not pass Space and Enter on to the slide deck.
 * - YouTube refuses to play an embedded video in a page that is opened from
 *   disk (file://, "error 153"). In that case the click opens the video on
 *   youtube.com in a new tab instead. The slide also has a plain link to the
 *   video, for any other case where an embed is not allowed.
 */
(function () {
  function shown(fig) {
    // Reveal.js marks the visible slide .present, the standalone deck marks it .on.
    var sec = fig.closest("section");
    return !!sec && (sec.classList.contains("present") || sec.classList.contains("on"));
  }

  function setup(fig) {
    var id = fig.getAttribute("data-yt");
    var original = fig.querySelector(".yt-play");
    if (!id || !original) { return; }
    var timer = null;

    function reset() {
      var frame = fig.querySelector(".yt-frame");
      if (frame) { fig.replaceChild(original, frame); }
      if (timer) { clearInterval(timer); timer = null; }
    }

    function play() {
      if (location.protocol === "file:") {
        window.open("https://www.youtube.com/watch?v=" + encodeURIComponent(id),
                    "_blank", "noopener,noreferrer");
        return;
      }
      var frame = document.createElement("iframe");
      frame.className = "yt-frame";
      frame.src = "https://www.youtube.com/embed/" + encodeURIComponent(id) +
        "?autoplay=1&rel=0&playsinline=1";
      frame.title = original.getAttribute("aria-label") || "Video";
      frame.setAttribute("allow",
        "autoplay; encrypted-media; picture-in-picture; fullscreen");
      frame.setAttribute("allowfullscreen", "");
      frame.setAttribute("referrerpolicy", "strict-origin-when-cross-origin");
      fig.replaceChild(frame, original);
      // stop the video as soon as the slide is left
      timer = setInterval(function () { if (!shown(fig)) { reset(); } }, 300);
    }

    original.addEventListener("click", play);
    original.addEventListener("keydown", function (event) {
      event.stopPropagation();           // Space / Enter must not move the slides
    });
    if (window.Reveal && Reveal.on) {
      Reveal.on("slidechanged", function () { if (!shown(fig)) { reset(); } });
    }
  }

  function init() {
    Array.prototype.forEach.call(document.querySelectorAll("figure.yt"), setup);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
