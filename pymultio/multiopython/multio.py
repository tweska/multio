import os

from .lib import ffi, lib
from .config import Config
from .handler import Handler
from .metadata import Metadata

class Multio:
    """This is the main interface class for Multio that users will interact with"""

    def __init__(self, config=None):

        if config == None:
            raise AttributeError("Config dictionary is required.")
        
        self.__conf = Config(**config)

        self.__handle = Handler(self.__conf)

        self.__metadata = None

    def __version__(self):
        tmp_str = ffi.new("char**")
        lib.multio_version(tmp_str)
        versionstr = ffi.string(tmp_str[0]).decode("utf-8")
        return versionstr

    def set_conf_path(self, conf_path):
        self.__conf.set_conf_path(conf_path)

    def start_server(self):
        self.__conf.start_server()

    def create_metadata(self, md=None):

        self.__metadata = Metadata(self.__handle, md=md)

    def open_connections(self):
        self.__handle.open_connections()
    
    def close_connections(self):
        self.__handle.close_connections()

    def flush(self):
        if self.__metadata != None:
            self.__handle.flush(self.__metadata)
        else:
            raise AttributeError(f"No metadata object instantiated")

    def notify(self):
        if self.__metadata != None:
            self.__handle.notify(self.__metadata)
        else:
            raise AttributeError(f"No metadata object instantiated")

    def write_domain(self, data):
        self.__handle.write_domain(self.__metadata, data, len(data))

    def write_mask(self, data):
        self.__handle.write_mask_float(self.__metadata, data, len(data))

    def write_field(self, data):
        self.__handle.write_field_float(self.__metadata, data, len(data))

    def field_accepted(self, accepted):
        self.__handle.field_accepted(self.__metadata, accepted)
    