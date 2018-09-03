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
