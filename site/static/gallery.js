/* Screenshot gallery. The pictures and captions are in the HTML.
   Escape closes the large view through this handler and the dialog's
   native cancel behavior. Arrow keys move between pictures. */
(function () {
  if (!window.HTMLDialogElement || typeof HTMLDialogElement.prototype.showModal !== "function") {
    return;
  }

  var root = document.querySelector("[data-shot-gallery]");
  var dialog = document.getElementById("shot-dialog");
  if (!root || !dialog) {
    return;
  }

  document.documentElement.classList.add("js");

  var slides = Array.prototype.slice.call(root.querySelectorAll(".shot-slide"));
  var thumbs = Array.prototype.slice.call(root.querySelectorAll(".shot-thumb"));
  var track = root.querySelector(".shot-track");
  var count = root.querySelector(".shot-count");
  var countLabel = root.querySelector(".shot-count-label");
  var dialogImg = dialog.querySelector(".shot-dialog-img");
  var dialogCaption = dialog.querySelector(".shot-dialog-caption");
  var dialogCount = dialog.querySelector(".shot-dialog-count");
  var dialogCountLabel = dialog.querySelector(".shot-dialog-count-label");
  var closeButton = dialog.querySelector(".shot-close");
  var countTemplate = root.getAttribute("data-count-template");
  var labelTemplate = root.getAttribute("data-label-template");
  var reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var index = 0;
  var lastFocus = null;
  var token = 0;
  var suppressScroll = false;
  var touchStartX = 0;
  var touchStartY = 0;

  function fill(template, current) {
    return template.replace("{current}", String(current)).replace("{total}", String(slides.length));
  }

  function writeStatus(current) {
    var countText = fill(countTemplate, current);
    var labelText = fill(labelTemplate, current);
    count.textContent = countText;
    countLabel.textContent = labelText;
    dialogCount.textContent = countText;
    dialogCountLabel.textContent = labelText;
  }

  function slideScrollLeft(slide) {
    var slideBox = slide.getBoundingClientRect();
    var trackBox = track.getBoundingClientRect();
    return slideBox.left - trackBox.left + track.scrollLeft;
  }

  function scrollToIndex(next) {
    suppressScroll = true;
    track.scrollTo({
      left: slideScrollLeft(slides[next]),
      behavior: reduceMotion ? "auto" : "smooth"
    });
    window.setTimeout(function () {
      suppressScroll = false;
    }, reduceMotion ? 40 : 500);
  }

  function showFull() {
    var slide = slides[index];
    var preview = slide.querySelector("img");
    var fullUrl = slide.getAttribute("data-full");
    var request = token + 1;
    token = request;
    dialogImg.alt = preview.getAttribute("alt") || "";
    dialogImg.width = Number(slide.getAttribute("data-full-width"));
    dialogImg.height = Number(slide.getAttribute("data-full-height"));
    dialogImg.setAttribute("aria-busy", "true");
    dialogCaption.textContent = slide.querySelector("figcaption").textContent;
    dialogImg.src = preview.currentSrc || preview.src;
    var full = new Image();
    full.onload = function () {
      if (request !== token || !dialog.open) {
        return;
      }
      dialogImg.src = fullUrl;
      dialogImg.removeAttribute("aria-busy");
    };
    full.onerror = function () {
      if (request !== token || !dialog.open) {
        return;
      }
      dialogImg.src = fullUrl;
      dialogImg.removeAttribute("aria-busy");
    };
    full.src = fullUrl;
  }

  function select(next, shouldScroll) {
    index = (next + slides.length) % slides.length;
    slides.forEach(function (slide, slideIndex) {
      var link = slide.querySelector("a");
      if (slideIndex === index) {
        link.removeAttribute("tabindex");
      } else {
        link.setAttribute("tabindex", "-1");
      }
    });
    thumbs.forEach(function (thumb, thumbIndex) {
      if (thumbIndex === index) {
        thumb.setAttribute("aria-current", "true");
      } else {
        thumb.removeAttribute("aria-current");
      }
    });
    writeStatus(index + 1);
    if (shouldScroll) {
      scrollToIndex(index);
    }
    if (dialog.open) {
      showFull();
    }
  }

  function openDialog(trigger) {
    lastFocus = trigger || null;
    showFull();
    if (!dialog.open) {
      dialog.showModal();
    }
    closeButton.focus();
  }

  slides.forEach(function (slide, slideIndex) {
    slide.querySelector("a").addEventListener("click", function (event) {
      event.preventDefault();
      select(slideIndex, false);
      openDialog(event.currentTarget);
    });
  });

  thumbs.forEach(function (thumb, thumbIndex) {
    thumb.addEventListener("click", function () {
      select(thumbIndex, true);
    });
  });

  root.querySelector(".shot-prev").addEventListener("click", function () {
    select(index - 1, true);
  });
  root.querySelector(".shot-next").addEventListener("click", function () {
    select(index + 1, true);
  });
  dialog.querySelector(".shot-dialog-prev").addEventListener("click", function () {
    select(index - 1, true);
  });
  dialog.querySelector(".shot-dialog-next").addEventListener("click", function () {
    select(index + 1, true);
  });
  closeButton.addEventListener("click", function () {
    dialog.close();
  });

  function trapTab(event) {
    if (event.key !== "Tab" || !dialog.open) {
      return;
    }
    var items = Array.prototype.filter.call(dialog.querySelectorAll("button"), function (button) {
      return !button.disabled;
    });
    if (!items.length) {
      return;
    }
    var first = items[0];
    var last = items[items.length - 1];
    var active = document.activeElement;
    if (event.shiftKey && (active === first || !dialog.contains(active))) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && (active === last || !dialog.contains(active))) {
      event.preventDefault();
      first.focus();
    }
  }

  function moveFromKey(event) {
    trapTab(event);
    if (event.key === "ArrowLeft") {
      event.preventDefault();
      select(index - 1, true);
    } else if (event.key === "ArrowRight") {
      event.preventDefault();
      select(index + 1, true);
    } else if (event.key === "Home") {
      event.preventDefault();
      select(0, true);
    } else if (event.key === "End") {
      event.preventDefault();
      select(slides.length - 1, true);
    } else if (event.key === "Escape" && dialog.open) {
      event.preventDefault();
      dialog.close();
    }
  }

  root.addEventListener("keydown", function (event) {
    if (dialog.open) {
      return;
    }
    moveFromKey(event);
  });
  dialog.addEventListener("keydown", moveFromKey);

  dialog.addEventListener("click", function (event) {
    if (event.target === dialog || event.target.classList.contains("shot-dialog-stage") || event.target.classList.contains("shot-dialog-image")) {
      dialog.close();
    }
  });

  dialog.addEventListener("close", function () {
    token = token + 1;
    dialogImg.removeAttribute("aria-busy");
    var currentLink = slides[index].querySelector("a");
    window.setTimeout(function () {
      if (currentLink && typeof currentLink.focus === "function") {
        currentLink.focus();
      }
    }, 0);
  });

  dialog.addEventListener("touchstart", function (event) {
    if (!event.changedTouches || !event.changedTouches.length) {
      return;
    }
    touchStartX = event.changedTouches[0].clientX;
    touchStartY = event.changedTouches[0].clientY;
  }, { passive: true });

  dialog.addEventListener("touchend", function (event) {
    if (!event.changedTouches || !event.changedTouches.length) {
      return;
    }
    var dx = event.changedTouches[0].clientX - touchStartX;
    var dy = event.changedTouches[0].clientY - touchStartY;
    if (Math.abs(dx) < 48 || Math.abs(dx) < Math.abs(dy)) {
      return;
    }
    select(index + (dx < 0 ? 1 : -1), true);
  }, { passive: true });

  document.addEventListener("focusin", function (event) {
    if (!dialog.open || dialog.contains(event.target)) {
      return;
    }
    closeButton.focus();
  });

  track.addEventListener("scroll", function () {
    if (suppressScroll || dialog.open) {
      return;
    }
    var midpoint = track.scrollLeft + (track.clientWidth / 2);
    var best = 0;
    var bestDistance = Infinity;
    slides.forEach(function (slide, slideIndex) {
      var left = slideScrollLeft(slide);
      var center = left + (slide.getBoundingClientRect().width / 2);
      var distance = Math.abs(center - midpoint);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = slideIndex;
      }
    });
    if (best !== index) {
      select(best, false);
    }
  }, { passive: true });

  select(0, false);
})();
