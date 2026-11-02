// innoextract's own extraction (cli/extract.cpp), without its command line.
#include "vg_installer.h"
#include <cstdio>
#include <exception>
#include "cli/extract.hpp"
#include "util/log.hpp"

extern "C" int vg_installer_extract(const char *setup,const char *out,char *error,size_t n){
    try{
        // innoextract's command-line defaults, as scripts/vengeance/prepare.py runs it.
        extract_options o;o.quiet=true;o.silent=true;o.extract=true;o.output_dir=out;
        o.collisions=OverwriteCollisions;o.codepage=0;o.extract_temp=true;o.extract_unknown=true;o.gog_galaxy=true;
        o.filenames.set_expand(true);o.filenames.set_lowercase(false);
        process_file(setup,o);
        if(logger::total_errors){std::snprintf(error,n,"the installer had %u errors",(unsigned)logger::total_errors);return -1;}
        return 0;
    }catch(const std::exception &e){std::snprintf(error,n,"%s",e.what());return -1;}
    catch(...){std::snprintf(error,n,"the installer could not be unpacked");return -1;}
}
