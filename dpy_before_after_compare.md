This is a lightly-edited terminal log of an informal before/after test I did on Adrian's doxypysql.py client. I'll start with an overview of the procedure:

1. Use Doxygen's master branch to generate `doxygen_docs/` from the source of my sql3 branch.
2. Use my sql3 branch to generate `doxygen_docs2/` from the source of my sql3 branch.
3. Run scripts with commands that exercise most of search.py's flags and capture the output of each.
4. Manually examine/evaluate what has shifted and how.

Here's the "before" script.

```ShellSession
[nix-shell:~/work/doxygen]$ cat doxypysql_before.sh
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -f generateSqlite3 > dpy_before/1.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -M ClassDef > dpy_before/2.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -M Definition > dpy_before/3.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -B ClassDef > dpy_before/4.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -S Definition > dpy_before/5.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -f generateXML > dpy_before/6.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -m ALGO_MD5 > dpy_before/7.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -m COMPILE_FOR_4_OPTIONS > dpy_before/8.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -v m_impl > dpy_before/9.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -R -v .* > dpy_before/10.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -R -F .* > dpy_before/11.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -R -t .* > dpy_before/12.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -i /Users/abathur/work/doxygen/src/dirdef.h > dpy_before/13.json
python addon/doxypysql/search.py -d doxygen_docs/doxygen_sqlite3.db -I /Users/abathur/work/doxygen/src/dirdef.h > dpy_before/14.json
```

I ran the above using the old doxypysql/search.py (lightly patched for python3) from the master branch. If you want to inspect the output for yourself, it's in https://github.com/abathur/doxygen/tree/dpysql/dpy_before

I also ran a roughly-identical script to populate `dpy_after` using the updated client and database. You can likewise inspect its output directly in https://github.com/abathur/doxygen/tree/dpysql/dpy_after

The client's record return order isn't deterministic, so I had to sort the output on each side before comparing.

```ShellSession
[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/1.json) <(sort dpy_after/1.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/2.json) <(sort dpy_after/2.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/3.json) <(sort dpy_after/3.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/4.json) <(sort dpy_after/4.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/5.json) <(sort dpy_after/5.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/6.json) <(sort dpy_after/6.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/7.json) <(sort dpy_after/7.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/8.json) <(sort dpy_after/8.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/9.json) <(sort dpy_after/9.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/10.json) <(sort dpy_after/10.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/11.json) <(sort dpy_after/11.json)
/dev/fd/63 /dev/fd/62 differ: char 2322, line 130

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/12.json) <(sort dpy_after/12.json)

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/13.json) <(sort dpy_after/13.json)
/dev/fd/63 /dev/fd/62 differ: char 18, line 1

[nix-shell:~/work/doxygen]$ cmp <(sort dpy_before/14.json) <(sort dpy_after/14.json)
```

11 & 13 differ. Let's see how badly. I'll do the easy one first:

```ShellSession
[nix-shell:~/work/doxygen]$ diff <(sort dpy_before/13.json) <(sort dpy_after/13.json)
1,3c1,3
<         "name": "definition.h"
<         "name": "qlist.h"
<         "name": "sortdict.h"
---
>         "name": "qtools/qlist.h"
>         "name": "src/definition.h"
>         "name": "src/sortdict.h"
```

They differ, but `after` has better path information.

```ShellSession
[nix-shell:~/work/doxygen]$ diff <(sort dpy_before/11.json) <(sort dpy_after/11.json)
176,178d175
<         "id": 215
...
<         "id": 713
686,1047c257
<         "name": "/Users/abathur/work/doxygen/src/arguments.cpp",
<         "name": "/Users/abathur/work/doxygen/src/arguments.h",
<         "name": "/Users/abathur/work/doxygen/src/bufstr.h",
<         "name": "/Users/abathur/work/doxygen/src/cite.cpp",
<         "name": "/Users/abathur/work/doxygen/src/cite.h",
<         "name": "/Users/abathur/work/doxygen/src/clangparser.cpp",
<         "name": "/Users/abathur/work/doxygen/src/clangparser.h",
<         "name": "/Users/abathur/work/doxygen/src/classdef.cpp",
<         "name": "/Users/abathur/work/doxygen/src/classdef.h",
<         "name": "/Users/abathur/work/doxygen/src/classlist.cpp",
<         "name": "/Users/abathur/work/doxygen/src/classlist.h",
<         "name": "/Users/abathur/work/doxygen/src/cmdmapper.cpp",
<         "name": "/Users/abathur/work/doxygen/src/cmdmapper.h",
<         "name": "/Users/abathur/work/doxygen/src/code.h",
...
```

Ok. Looks like the `before` file has many more IDs, and it has unstripped paths. Of course! I'll trim those...

```ShellSession
[nix-shell:~/work/doxygen]$ sed -e 's/\/Users\/abathur\/work\/doxygen\///' dpy_before/11.json > dpy_before/11_strip_prefix.json

[nix-shell:~/work/doxygen]$ diff -y dpy_before/11_strip_prefix.json dpy_after/11.json
[                                                               [
    {                                                               {
        "name": "src/util.cpp",                                         "name": "src/util.cpp",
        "id": 1                                                         "id": 1
    },                                                              },
    {                                                               {
        "name": "no-file",                                    |         "name": "src/docparser.cpp",
        "id": 2                                                         "id": 2
    },                                                              },
    {                                                               {
        "name": "src/docparser.cpp",                          |         "name": "src/latexdocvisitor.h",
        "id": 3                                                         "id": 3
    },                                                              },
    {                                                               {
        "name": "src/latexdocvisitor.h",                      |         "name": "src/context.h",
        "id": 4                                                         "id": 4
    },                                                              },
    {                                                               {
        "name": "src/context.h",                              |         "name": "src/context.cpp",
        "id": 5                                                         "id": 5
    },                                                              },
    {                                                               {
        "name": "context.h",                                  |         "name": "src/index.cpp",
        "id": 6                                                         "id": 6
    },                                                              },
    {                                                               {
        "name": "src/context.cpp",                            |         "name": "src/arguments.h",
        "id": 7                                                         "id": 7
    },                                                              },
    {                                                               {
        "name": "src/index.cpp",                              |         "name": "src/arguments.cpp",
        "id": 8                                                         "id": 8
    },                                                              },
    {                                                               {
        "name": "src/arguments.h",                            |         "name": "src/template.cpp",
        "id": 9                                                         "id": 9
    },                                                              },
    {                                                               {
        "name": "arguments.h",                                |         "name": "src/classdef.h",
        "id": 10                                                        "id": 10
    },                                                              },
    {                                                               {
        "name": "src/arguments.cpp",                          |         "name": "src/entry.h",
        "id": 11                                                        "id": 11
    },                                                              },
    ...                                                             ...
```

Even after stripping path prefixes, files still differ because the un-updated generator records `no-file` for some things and creates duplicate file entries for header files.

Since the records won't align or have the same quantity, I'll use `sort|uniq` to filter out the JSON furniture to help highlight what still differs.

```ShellSession
[nix-shell:~/work/doxygen]$ diff <(sort dpy_before/11_strip_prefix.json|uniq) <(sort dpy_after/11.json|uniq)
130,134d129
<         "id": 215
...
<         "id": 713
714,893d254
<         "name": "CharStream.h",
<         "name": "ErrorHandler.h",
<         "name": "JavaCC.h",
<         "name": "ParseException.h",
<         "name": "Token.h",
<         "name": "TokenManager.h",
<         "name": "TokenMgrError.h",
<         "name": "VhdlParser.h",
<         "name": "VhdlParserConstants.h",
<         "name": "VhdlParserErrorHandler.hpp",
<         "name": "VhdlParserIF.h",
<         "name": "VhdlParserTokenManager.h",
<         "name": "arguments.h",
<         "name": "assert.h",
<         "name": "bufstr.h",
<         "name": "cite.h",
<         "name": "clangparser.h",
<         "name": "classdef.h",
<         "name": "classlist.h",
<         "name": "cmdmapper.h",
<         "name": "code.h",
<         "name": "commentcnv.h",
<         "name": "commentscan.h",
<         "name": "condparser.h",
<         "name": "config.h",
<         "name": "configimpl.h",
<         "name": "configvalues.h",
<         "name": "constexp.h",
<         "name": "context.h",
<         "name": "cppvalue.h",
<         "name": "ctype.h",
<         "name": "debug.h",
<         "name": "declinfo.h",
<         "name": "defargs.h",
<         "name": "defgen.h",
<         "name": "define.h",
<         "name": "definition.h",
<         "name": "dia.h",
<         "name": "diagram.h",
<         "name": "dirdef.h",
<         "name": "docbookgen.h",
<         "name": "docbookvisitor.h",
<         "name": "docparser.h",
<         "name": "docsets.h",
<         "name": "doctokenizer.h",
<         "name": "docvisitor.h",
<         "name": "dot.h",
<         "name": "doxygen.h",
<         "name": "eclipsehelp.h",
<         "name": "entry.h",
<         "name": "errno.h",
<         "name": "example.h",
<         "name": "exception",
<         "name": "filedef.h",
<         "name": "filename.h",
<         "name": "fileparser.h",
<         "name": "filestorage.h",
<         "name": "formula.h",
<         "name": "fortranscanner.h",
<         "name": "ftextstream.h",
<         "name": "ftvhelp.h",
<         "name": "groupdef.h",
<         "name": "growbuf.h",
<         "name": "htags.h",
<         "name": "htmlattrib.h",
<         "name": "htmldocvisitor.h",
<         "name": "htmlentity.h",
<         "name": "htmlgen.h",
<         "name": "htmlhelp.h",
<         "name": "image.h",
<         "name": "index.h",
<         "name": "lang_cfg.h",
<         "name": "language.h",
<         "name": "latexdocvisitor.h",
<         "name": "latexgen.h",
<         "name": "layout.h",
<         "name": "layout_default.xml.h",
<         "name": "limits.h",
<         "name": "locale.h",
<         "name": "lodepng.h",
<         "name": "logos.h",
<         "name": "mandocvisitor.h",
<         "name": "mangen.h",
<         "name": "markdown.h",
<         "name": "marshal.h",
<         "name": "math.h",
<         "name": "md5.h",
<         "name": "memberdef.h",
<         "name": "membergroup.h",
<         "name": "memberlist.h",
<         "name": "membername.h",
<         "name": "memory.h",
<         "name": "message.h",
<         "name": "msc.h",
<         "name": "namespacedef.h",
<         "name": "no-file",
<         "name": "objcache.h",
<         "name": "outputgen.h",
<         "name": "outputlist.h",
<         "name": "pagedef.h",
<         "name": "parserintf.h",
<         "name": "perlmodgen.h",
<         "name": "plantuml.h",
<         "name": "portable.h",
<         "name": "pre.h",
<         "name": "printdocvisitor.h",
<         "name": "pyscanner.h",
<         "name": "qarray.h",
<         "name": "qcache.h",
<         "name": "qcstring.h",
<         "name": "qcstringlist.h",
<         "name": "qdatetime.h",
<         "name": "qdict.h",
<         "name": "qdir.h",
<         "name": "qfile.h",
<         "name": "qfileinfo.h",
<         "name": "qglobal.h",
<         "name": "qgstring.h",
<         "name": "qhp.h",
<         "name": "qhpxmlwriter.h",
<         "name": "qintdict.h",
<         "name": "qiodevice.h",
<         "name": "qlist.h",
<         "name": "qmap.h",
<         "name": "qmutex.h",
<         "name": "qptrdict.h",
<         "name": "qqueue.h",
<         "name": "qregexp.h",
<         "name": "qstack.h",
<         "name": "qstring.h",
<         "name": "qstrlist.h",
<         "name": "qtextcodec.h",
<         "name": "qtextstream.h",
<         "name": "qthread.h",
<         "name": "qtools/qarray.h",
<         "name": "qtools/qasciidict.h",
<         "name": "qtools/qcollection.h",
<         "name": "qtools/qconfig.h",
<         "name": "qtools/qcstring.h",
<         "name": "qtools/qdatastream.h",
<         "name": "qtools/qdatetime.h",
<         "name": "qtools/qfeatures.h",
<         "name": "qtools/qfile.h",
<         "name": "qtools/qfileinfo.h",
<         "name": "qtools/qgarray.h",
<         "name": "qtools/qgcache.h",
<         "name": "qtools/qgdict.h",
<         "name": "qtools/qglist.h",
<         "name": "qtools/qglobal.h",
<         "name": "qtools/qgvector.h",
<         "name": "qtools/qinternallist.h",
<         "name": "qtools/qiodevice.h",
<         "name": "qtools/qmap.h",
<         "name": "qtools/qmodules.h",
<         "name": "qtools/qregexp.h",
<         "name": "qtools/qshared.h",
<         "name": "qtools/qstring.h",
<         "name": "qtools/qstringlist.h",
<         "name": "qtools/qstrlist.h",
<         "name": "qtools/qtextstream.h",
<         "name": "qtools/qvaluelist.h",
<         "name": "qtools/qvaluestack.h",
<         "name": "qvaluelist.h",
<         "name": "qvector.h",
<         "name": "qwaitcondition.h",
<         "name": "qxml.h",
<         "name": "reflist.h",
<         "name": "resourcemgr.h",
<         "name": "rtfdocvisitor.h",
<         "name": "rtfgen.h",
<         "name": "rtfstyle.h",
<         "name": "scanner.h",
<         "name": "searchindex.h",
<         "name": "section.h",
<         "name": "settings.h",
<         "name": "signal.h",
<         "name": "sortdict.h",
<         "name": "sqlcode.h",
<         "name": "sqlite3gen.h",
<         "name": "sqlscanner.h",
949a311
>         "name": "src/doxygen.md",
1043a406
>         "name": "src/qtbc.h",
1133,1198d495
<         "name": "stdarg.h",
<         "name": "stdint.h",
<         "name": "stdio.h",
<         "name": "stdlib.h",
<         "name": "store.h",
<         "name": "string",
<         "name": "string.h",
<         "name": "sys/stat.h",
<         "name": "sys/types.h",
<         "name": "sys/wait.h",
<         "name": "tagreader.h",
<         "name": "tclscanner.h",
<         "name": "template.h",
<         "name": "textdocvisitor.h",
<         "name": "tooltip.h",
<         "name": "translator.h",
<         "name": "translator_adapter.h",
<         "name": "translator_am.h",
<         "name": "translator_ar.h",
<         "name": "translator_br.h",
<         "name": "translator_ca.h",
<         "name": "translator_cn.h",
<         "name": "translator_cz.h",
<         "name": "translator_de.h",
<         "name": "translator_dk.h",
<         "name": "translator_en.h",
<         "name": "translator_eo.h",
<         "name": "translator_es.h",
<         "name": "translator_fa.h",
<         "name": "translator_fi.h",
<         "name": "translator_fr.h",
<         "name": "translator_gr.h",
<         "name": "translator_hr.h",
<         "name": "translator_hu.h",
<         "name": "translator_id.h",
<         "name": "translator_it.h",
<         "name": "translator_je.h",
<         "name": "translator_jp.h",
<         "name": "translator_ke.h",
<         "name": "translator_kr.h",
<         "name": "translator_lt.h",
<         "name": "translator_lv.h",
<         "name": "translator_mk.h",
<         "name": "translator_nl.h",
<         "name": "translator_no.h",
<         "name": "translator_pl.h",
<         "name": "translator_pt.h",
<         "name": "translator_ro.h",
<         "name": "translator_ru.h",
<         "name": "translator_sc.h",
<         "name": "translator_si.h",
<         "name": "translator_sk.h",
<         "name": "translator_sr.h",
<         "name": "translator_sv.h",
<         "name": "translator_tr.h",
<         "name": "translator_tw.h",
<         "name": "translator_ua.h",
<         "name": "translator_vi.h",
<         "name": "translator_za.h",
<         "name": "types.h",
<         "name": "unistd.h",
<         "name": "util.h",
<         "name": "version.h",
<         "name": "vhdlcode.h",
<         "name": "vhdldocgen.h",
<         "name": "vhdljjparser.h",
1212,1216d508
<         "name": "vhdlstring.h",
<         "name": "xmlcode.h",
<         "name": "xmldocvisitor.h",
<         "name": "xmlgen.h",
<         "name": "xmlscanner.h",
```

This diff shows that the `after` output has a few modest improvements:
- We saw that `before` had duplicate entries for .h files with and without a directory. Since the src/*.h copies don't show up here, we know the `after` copy has all of these. Since the *.h copies do, we know that the `after` side lacks the duplicates.
- includes src/doxygen.md (because PageDef is now collected)
- includes src/qtbc.h (because DirDef is now collected; Doxygen doesn't otherwise encounter qtbc because the Doxyfile doesn't predefine USE_SQLITE3)
