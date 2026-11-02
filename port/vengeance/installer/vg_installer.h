/* Unpacking the Tribes: Vengeance community release on the player's own device
   (the in-app download, port/apple/vengeance_download.m). The release is a ZIP
   (Deflate64) holding an Inno Setup installer; these do what
   scripts/vengeance/prepare.py does on a Mac with 7-Zip and innoextract,
   without running the installer. Built by scripts/build-vengeance-installer.py. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Writes each named member of the ZIP into dir (the release's three installer
   files). Returns 0, or -1 with a reason in error. Only stored, Deflate and
   Deflate64 members; names with a path or "..", and members not listed, are
   refused. *done counts the bytes written so far, for a progress bar. */
int vg_installer_unzip(const char *zip, const char *dir, const char *const names[], size_t count,
                       volatile uint64_t *done, char *error, size_t error_size);
/* Unpacks the installer whose first file is setup into out (innoextract). */
int vg_installer_extract(const char *setup, const char *out, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
