# Third-party notices

This is a modified C++ port of FilmLog's contact-sheet renderer, with the original FilmLog license and copyright retained in LICENSE. The film-strip resources are copied from the user's FilmLog project at commit 7c9a250 and remain subject to their original terms.

Qt 6.8.3 is dynamically linked and distributed under LGPL-3.0 (with applicable Qt exceptions and third-party component terms). DLLs may be replaced with compatible versions. No restriction is imposed on reverse engineering for debugging modifications to these LGPL libraries. The application source and CMake build configuration are included in the source delivery.

Qt source for the exact version: https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/ (qtbase, qtimageformats and qtsvg). Qt licensing information: https://www.qt.io/licensing/open-source-lgpl-obligations . Component notices are included under licenses, including Qt SPDX metadata and bundled library licenses.

The embedded qtbase_zh_CN.qm translation is from Qt 6.8.3 qttranslations, distributed under the corresponding Qt license. Its source is available in the qttranslations submodule at the same versioned source location above.

MinGW GCC runtime libraries are distributed with the GCC Runtime Library Exception. Winpthreads retains its own permissive license. Corresponding notices are included under licenses/gcc and licenses/winpthreads.

No third-party font is bundled. The UI and default header font use system Microsoft YaHei UI (or system fallback). Users can choose other installed fonts or import their own fonts; imported font files are copied into saved preset assets. Sample illustrations used for verification are generated locally and do not contain user photographs. The Film Index logo is an original vector mark created for this application; its source and generation script are included.
