import curses.ascii
import io
import logging
import multiprocessing
import multiprocessing.pool
import os
import pickle
import re
import signal
import sys
import time

logger = logging.getLogger(__name__)

COLOR_RED = "\033[1;31m"
COLOR_YELLOW = "\033[1;33m"
COLOR_CYAN = "\033[0;36m"
COLOR_GREEN = "\033[0;32m"
COLOR_RESET = "\033[0;0m"

def get_workspace_path(resource):
    return os.path.abspath(os.environ["INET_ROOT"] + "/../" + resource)

def flatten(list):
    return [item for sublist in list for item in sublist]

def repr(object):
    return f"{object.__class__.__name__}({', '.join([f'{prop}={value}' for prop, value in object.__dict__.items()])})"

def coalesce(*values):
    """Return the first non-None value or None if all values are None"""
    return next((v for v in values if v is not None), None)

def convert_to_seconds(s):
    seconds_per_unit = {"ns": 1E-9, "us": 1E-6, "ms": 1E-3, "s": 1, "second": 1, "m": 60, "min": 60, "h": 3600, "hour": 3600, "d": 86400, "day": 86400, "w": 604800, "week": 604800}
    match = re.match("(-?[0-9]*\.?[0-9]*) *([a-zA-Z]+)", s)
    return float(match.group(1)) * seconds_per_unit[match.group(2)]

def write_object(file_name, object):
    with open(file_name, "wb") as file:
        pickle.dump(object, file)

def read_object(file_name):
    with open(file_name, "rb") as file:
        return pickle.load(file)

def matches_filter(value, positive_filter, negative_filter, full_match):
    return ((re.fullmatch(positive_filter, value) if full_match else re.search(positive_filter, value)) is not None if positive_filter else True) and \
           ((re.fullmatch(negative_filter, value) if full_match else re.search(negative_filter, value)) is None if negative_filter else True)

class KeyboardInterruptHandler:
    def __init__(self):
        self.enabled = True
        self.old_handler = None
        self.received_signal = None

    def handle_disabled_keyboard_interrupt(self, sig, frame):
        self.received_signal = (sig, frame)
        logger.debug("SIGINT received, delaying KeyboardInterrupt.")

    def handle_pending_keyboard_interrupt(self):
        if self.received_signal:
            self.old_handler(*self.received_signal)
            self.received_signal = None

    def disable(self):
        if self.enabled:
            self.enabled = False
            self.old_handler = signal.signal(signal.SIGINT, self.handle_disabled_keyboard_interrupt)
            self.received_signal = None

    def enable(self):
        if not self.enabled:
            self.enabled = True
            signal.signal(signal.SIGINT, self.old_handler)
            self.handle_pending_keyboard_interrupt()

class EnabledKeyboardInterrupts:
    def __init__(self, handler):
        self.handler = handler
        
    def __enter__(self):
        if self.handler:
            try:
                self.handler.enable()
            except:
                if self.__exit__(*sys.exc_info()):
                    pass
                else:
                    raise
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.handler:
            self.handler.disable()

class DisabledKeyboardInterrupts:
    def __init__(self, handler):
        self.handler = handler
        
    def __enter__(self):
        if self.handler:
            try:
                self.handler.disable()
            except:
                if self.__exit__(*sys.exc_info()):
                    pass
                else:
                    raise

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.handler:
            self.handler.enable()

def test_keyboard_interrupt_handler(a, b):
    handler = KeyboardInterruptHandler()
    with DisabledKeyboardInterrupts(handler):
        print("Disabled start")
        time.sleep(a)
        try:
            with EnabledKeyboardInterrupts(handler):
                print("Enabled start")
                time.sleep(b)
                print("Enabled end")
        except KeyboardInterrupt:
            print("Interrupted")
        print("Disabled end")

def map_sequentially_or_concurrently(elements, function, concurrent=None, randomize=False, chunksize=1, **kwargs):
    element_count = len(elements)
    def call_with_capturing_output(element, **kwargs):
        output_stream = io.StringIO()
        element_index = elements.index(element)
        result = function(element, output_stream=output_stream, index=element_index, count=element_count, **kwargs)
        print(output_stream.getvalue(), end="")
        return result
    for element in elements:
        element.set_cancel(False)
    if randomize:
        elements = random.sample(elements, k=len(elements))
    if concurrent:
        try:
            pool = multiprocessing.pool.ThreadPool(multiprocessing.cpu_count())
            results = pool.map_async(lambda element: call_with_capturing_output(element, **kwargs), elements, chunksize=chunksize)
            return results.get(0xFFFF)
        except KeyboardInterrupt:
            for element in elements:
                element.set_cancel(True)
            return results.get(0xFFFF)
    else:
        keyboard_interrupt_handler = KeyboardInterruptHandler()
        with DisabledKeyboardInterrupts(keyboard_interrupt_handler):
            results = []
            cancel = False
            element_index = 0
            for element in elements:
                result = function(element, keyboard_interrupt_handler=keyboard_interrupt_handler, cancel=cancel, index=element_index, count=element_count, **kwargs)
                if result.result == "CANCEL":
                    cancel = True
                results.append(result)
                element_index = element_index + 1
            return results
