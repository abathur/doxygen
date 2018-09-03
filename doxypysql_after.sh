python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -f generateSqlite3 > dpy_after/1.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -M ClassDef > dpy_after/2.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -M Definition > dpy_after/3.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -B ClassDef > dpy_after/4.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -S Definition > dpy_after/5.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -f generateXML > dpy_after/6.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -m ALGO_MD5 > dpy_after/7.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -m COMPILE_FOR_4_OPTIONS > dpy_after/8.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -v m_impl > dpy_after/9.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -R -v .* > dpy_after/10.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -R -F .* > dpy_after/11.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -R -t .* > dpy_after/12.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -i src/dirdef.h > dpy_after/13.json
python addon/doxypysql/search.py -d doxygen_docs2/doxygen_sqlite3.db -I src/dirdef.h > dpy_after/14.json
