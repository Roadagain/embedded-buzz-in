"""Play sounds for BOOT notifications, first-place presses and promoted answerers."""

import logging
from pathlib import Path
import re
import shutil
import subprocess
import threading


PROMOTION_DELAY_SECONDS = 1.0
LOGGER = logging.getLogger("bridge")
PRESS = re.compile(r"PRESS child=([1-9]\d*) rank=([1-9]\d*)(?: elapsed_ms=\d+)?")
WRONG = re.compile(r"JUDGE result=wrong child=[1-9]\d* next_child=(0|[1-9]\d*)")


class PressSound:
    """Play BOOT, first-place and promotion sounds without queuing."""

    def __init__(self, filename):
        self.lock = threading.RLock()
        self.pending_promotion = None
        self.process = None
        self.retired = []
        self.filename = None
        self.player = None
        if filename is None:
            return
        path = Path(filename).expanduser().resolve()
        self.player = shutil.which("afplay")
        if not path.is_file() or self.player is None:
            LOGGER.warning("Press sound disabled: sound file or afplay not found: %s", path)
            return
        self.filename = str(path)

    def handle_line(self, line):
        with self.lock:
            self._handle_line(line)

    def _handle_line(self, line):
        # Reap interrupted playback without waiting in the serial receive loop.
        self.retired = [process for process in self.retired if process.poll() is None]
        if self.process is not None:
            status = self.process.poll()
            if status is not None:
                if status != 0:
                    LOGGER.warning("Press sound playback failed (exit status %s)", status)
                self.process = None

        should_play = False
        if line in ("BOOT", "RESET") or line.startswith("JUDGE result=correct child="):
            self._cancel_promotion()
        if line == "BOOT":
            # A new boot must sound even if previous playback is still active.
            self._play(promoted=True)
            return
        press = PRESS.fullmatch(line)
        wrong = WRONG.fullmatch(line)
        if press is not None:
            rank = int(press.group(2))
            if rank == 1:
                self._cancel_promotion()
                should_play = True
        elif wrong is not None:
            self._cancel_promotion()
            if self.filename is not None and int(wrong.group(1)) != 0:
                timer = threading.Timer(
                    PROMOTION_DELAY_SECONDS, lambda: self._play_promotion(timer)
                )
                timer.daemon = True
                self.pending_promotion = timer
                timer.start()
        if should_play:
            self._play(promoted=False)

    def _cancel_promotion(self):
        if self.pending_promotion is not None:
            self.pending_promotion.cancel()
            self.pending_promotion = None

    def _play_promotion(self, timer):
        with self.lock:
            # A canceled callback may already be waiting for this lock.
            if self.pending_promotion is not timer:
                return
            self.pending_promotion = None
            self._play(promoted=True)

    def _play(self, *, promoted):
        if self.filename is None:
            return
        if self.process is not None:
            if not promoted:
                LOGGER.debug("Press sound skipped: playback already active")
                return
            # A promotion must sound even immediately after the first press.
            self.process.terminate()
            self.retired.append(self.process)
            self.process = None
        try:
            self.process = subprocess.Popen(
                [self.player, self.filename],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            LOGGER.info("Playing press sound: %s", self.filename)
        except OSError as error:
            LOGGER.warning("Press sound could not start: %s", error)

    def reset(self):
        """Stop playback and cancel the previous connection's pending sound."""
        with self.lock:
            self.close()

    def close(self):
        with self.lock:
            self._cancel_promotion()
            self._close()

    def _close(self):
        processes = self.retired + ([self.process] if self.process is not None else [])
        for process in processes:
            if process.poll() is not None:
                continue
            process.terminate()
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        self.process = None
        self.retired = []
