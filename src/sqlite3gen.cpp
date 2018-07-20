/******************************************************************************
 *
 * Copyright (C) 1997-2015 by Dimitri van Heesch.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License is hereby
 * granted. No representations are made about the suitability of this software
 * for any purpose. It is provided "as is" without express or implied warranty.
 * See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "settings.h"
#include "message.h"

#if USE_SQLITE3

#include "qtbc.h"
#include "sqlite3gen.h"
#include "doxygen.h"
#include "xmlgen.h"
#include "xmldocvisitor.h"
#include "config.h"
#include "util.h"
#include "outputlist.h"
#include "docparser.h"
#include "language.h"

#include "version.h"
#include "dot.h"
#include "arguments.h"
#include "classlist.h"
#include "filedef.h"
#include "namespacedef.h"
#include "filename.h"
#include "groupdef.h"
#include "membername.h"
#include "memberdef.h"
#include "pagedef.h"
#include "dirdef.h"
#include "section.h"

#include <qdir.h>
#include <string.h>
#include <sqlite3.h>

//#define DBG_CTX(x) printf x
#define DBG_CTX(x) do { } while(0)

// used by sqlite3_trace in generateSqlite3()
static void sqlLog(void *dbName, const char *sql){
  msg("SQL: '%s'\n", sql);
}

const char * table_schema[][2] = {
  /* TABLES */
  { "meta",
    "CREATE TABLE IF NOT EXISTS meta (\n"
      "\t-- Information about this db and how it was generated.\n"
      "\t-- Doxygen info\n"
      "\tdoxygen_version    TEXT PRIMARY KEY NOT NULL,\n"
      // Doxygen's version is likely to rollover much faster than the schema, and at least until it becomes a core output format, we might want to make fairly large schema changes even on minor iterations for Doxygen itself. If these tools just track a predefined semver schema version that can iterate independently, it *might* not be as hard to keep them in sync?
      "\tschema_version     TEXT NOT NULL, -- Schema-specific semver\n"
      "\t-- run info\n"
      "\tgenerated_at       TEXT NOT NULL,\n"
      "\tgenerated_on       TEXT NOT NULL,\n"
      "\t-- project info\n"
      "\tproject_name       TEXT NOT NULL,\n"
      "\tproject_number     TEXT,\n"
      "\tproject_brief      TEXT\n"
    ");"
  },
  //TODO: We could document all config options (probably with a separate config table), but I think this idea can wait until someone actually demonstrates a clear need.
  { "includes",
    "CREATE TABLE IF NOT EXISTS includes (\n"
      "\t-- #include relations.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tlocal        INTEGER NOT NULL,\n"
      "\tsrc_id       INTEGER NOT NULL REFERENCES file, -- File id of the includer.\n"
      "\tdst_id       INTEGER NOT NULL REFERENCES file, -- File id of the includee.\n"
      /* In theory we could include name here to be informationally equivalent with the XML, but I don't see an obvious use for it. */
      "\tUNIQUE(local, src_id, dst_id) ON CONFLICT IGNORE\n"
    ");"
  },
  { "contains",
    "CREATE TABLE IF NOT EXISTS contains (\n"
      "\t-- inner/outer relations (file, namespace, dir, class, group, page)\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_rowid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_rowid  INTEGER NOT NULL REFERENCES compounddef,\n"
    ");"
  },
  /* TODO: File can also share rowids with refid/compounddef/def. (It could
   *       even collapse into that table...)
   *
   * I took a first swing at this by changing insertFile() to:
   * - accept a FileDef
   * - make its own call to insertRefid
   * - return a refid struct.
   *
   * I rolled this back when I had trouble getting a FileDef for all types (PageDef in particular).
   *
   * Note: all colums referencing file would need an update.
   */
  { "file",
    "CREATE TABLE IF NOT EXISTS file (\n"
      "\t-- Names of source files and includes.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tname         TEXT NOT NULL\n"
    ");"
  },
  { "refid",
    "CREATE TABLE IF NOT EXISTS refid (\n"
      "\t-- Distinct refid for all documented entities.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\trefid        TEXT NOT NULL UNIQUE\n"
    ");"
  },
  { "xrefs",
    "CREATE TABLE IF NOT EXISTS xrefs (\n"
      "\t-- Cross-reference relation\n"
      "\t-- (combines xml <referencedby> and <references> nodes).\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tsrc_rowid    INTEGER NOT NULL REFERENCES refid, -- referrer id.\n"
      "\tdst_rowid    INTEGER NOT NULL REFERENCES refid, -- referee id.\n"
      "\tcontext      TEXT NOT NULL, -- inline, argument, initializer\n"
      "\t-- Just need to know they link; ignore duplicates.\n"
      "\tUNIQUE(src_rowid, dst_rowid, context) ON CONFLICT IGNORE\n"
    ");\n"
  },
  { "memberdef",
    "CREATE TABLE IF NOT EXISTS memberdef (\n"
      "\t-- All processed identifiers.\n"
      "\trowid                INTEGER PRIMARY KEY NOT NULL,\n"
      "\tname                 TEXT NOT NULL,\n"
      "\tdefinition           TEXT,\n"
      "\ttype                 TEXT,\n"
      "\targsstring           TEXT,\n"
      "\tscope                TEXT,\n"
      "\tinitializer          TEXT,\n"
      "\tbitfield             TEXT,\n"
      "\tread                 TEXT,\n"
      "\twrite                TEXT,\n"
      "\tprot                 INTEGER DEFAULT 0, -- 0:public 1:protected 2:private 3:package\n"
      "\tstatic               INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tconst                INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\texplicit             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tinline               INTEGER DEFAULT 0, -- 0:no 1:yes 2:both (set after encountering inline and not-inline\n"
      "\tfinal                INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tsealed               INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tnew                  INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\toptional             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\trequired             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tvolatile             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tvirt                 INTEGER DEFAULT 0, -- 0:no 1:virtual 2:pure-virtual\n"
      "\tmutable              INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tinitonly             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tattribute            INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tproperty             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\treadonly             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tbound                INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tconstrained          INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\ttransient            INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tmaybevoid            INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tmaybedefault         INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tmaybeambiguous       INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\treadable             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\twritable             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tgettable             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tprivategettable      INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tprotectedgettable    INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tsettable             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tprivatesettable      INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tprotectedsettable    INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\taccessor             INTEGER DEFAULT 0, -- 0:no 1:assign 2:copy 3:retain 4:string 5:weak\n"
      "\taddable              INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tremovable            INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\traisable             INTEGER DEFAULT 0, -- 0:no 1:yes\n"
      "\tkind                 TEXT NOT NULL, -- 'macro definition' 'function' 'variable' 'typedef' 'enumeration' 'enumvalue' 'signal' 'slot' 'friend' 'dcop' 'property' 'event' 'interface' 'service'\n"
      "\tbodystart            INTEGER DEFAULT 0, -- starting line of definition\n"
      "\tbodyend              INTEGER DEFAULT 0, -- ending line of definition\n"
      "\tbodyfile_id          INTEGER DEFAULT 0 REFERENCES file, -- file of definition\n"
      "\tfile_id              INTEGER NOT NULL REFERENCES file,  -- file where this identifier is located\n"
      "\tline                 INTEGER NOT NULL,  -- line where this identifier is located\n"
      "\tcolumn               INTEGER NOT NULL,  -- column where this identifier is located\n"
      "\tdetaileddescription  TEXT,\n"
      "\tbriefdescription     TEXT,\n"
      "\tinbodydescription    TEXT,\n"
      "\tFOREIGN KEY (rowid) REFERENCES refid (rowid)\n"
    ");"
  },
  { "member",
    "CREATE TABLE IF NOT EXISTS member (\n"
      "\t-- Memberdef <-> containing compound relation.\n"
      "\t-- Similar to XML listofallmembers.\n"
      "\trowid            INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tscope_rowid      INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tmemberdef_rowid  INTEGER NOT NULL REFERENCES memberdef,\n"
      "\tprot             INTEGER NOT NULL,\n"
      "\tvirt             INTEGER NOT NULL,\n"
      "\tambiguityscope   TEXT,\n"
      "\tUNIQUE(scope_rowid, memberdef_rowid)\n"
    ");"
  },
  { "reimplements",
    "CREATE TABLE IF NOT EXISTS reimplements (\n"
      "\t-- Inherited member reimplmentation relations.\n"
      "\trowid                  INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tmemberdef_rowid        INTEGER NOT NULL REFERENCES memberdef, -- reimplementing memberdef id.\n"
      "\treimplemented_rowid    INTEGER NOT NULL REFERENCES memberdef, -- reimplemented memberdef id.\n"
      "\tUNIQUE(memberdef_rowid, reimplemented_rowid) ON CONFLICT IGNORE\n"
    ");\n"
  },
  { "compounddef",
    "CREATE TABLE IF NOT EXISTS compounddef (\n"
      "\t-- Class/struct definitions.\n"
      "\trowid                INTEGER PRIMARY KEY NOT NULL,\n"
      "\tname                 TEXT NOT NULL,\n"
      "\ttitle                TEXT,\n"
      "\tkind                 TEXT NOT NULL, -- 'category' 'class' 'dir' 'enum' 'example' 'exception' 'file' 'group' 'interface' 'library' 'module' 'namespace' 'package' 'page' 'protocol' 'service' 'singleton' 'struct' 'type' 'union' 'unknown' ''\n" // yes, it can be an empty string
      "\tprot                 INTEGER,\n"
      "\tfile_id              INTEGER NOT NULL,\n"
      "\tline                 INTEGER NOT NULL,\n"
      "\tcolumn               INTEGER NOT NULL,\n"
      "\tdetaileddescription  TEXT,\n"
      "\tbriefdescription     TEXT,\n"
      "\tFOREIGN KEY (rowid) REFERENCES refid (rowid)\n"
    ");"
  },
  { "compoundref",
    "CREATE TABLE IF NOT EXISTS compoundref (\n"
      "\t-- Inheritance relation.\n"
      "\trowid          INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tbase_rowid     INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tderived_rowid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tprot           INTEGER NOT NULL,\n"
      "\tvirt           INTEGER NOT NULL,\n"
      "\tUNIQUE(base_rowid, derived_rowid)\n"
    ");"
  },
  { "param",
    "CREATE TABLE IF NOT EXISTS param (\n"
      "\t-- All processed parameters.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tattributes   TEXT,\n"
      "\ttype         TEXT,\n"
      "\tdeclname     TEXT,\n"
      "\tdefname      TEXT,\n"
      "\tarray        TEXT,\n"
      "\tdefval       TEXT,\n"
      "\tbriefdescription TEXT\n"
    ");"
    "CREATE UNIQUE INDEX idx_param ON param\n"
      "\t(type, defname);"
  },
  { "memberdef_param",
    "CREATE TABLE IF NOT EXISTS memberdef_param (\n"
      "\t-- Junction table for memberdef parameters.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tmemberdef_id INTEGER NOT NULL REFERENCES memberdef,\n"
      "\tparam_id     INTEGER NOT NULL REFERENCES param\n"
    ");"
  },
};
  const char * view_schema[][2] = {
  /* VIEWS */
  // Set up views AFTER we build the database, so that they can be indexed, but so we don't have to pay a performance penalty for inserts as we build.
  {
    /*
    Makes all reference/relation tables easier to use. For example:
    1. query xrefs and join this view on either xrefs.dst_rowid=def.rowid or xrefs.src_rowid=def.rowid
    3. receive everything you need to output a list of references to or from an entity

    It also supports a simple name search/lookup that generalizes compound and member types.

    NOTES:
      - summary here is kinda cheating; for compounds it generalizes title and briefdescription because there's no single field that works as a quick introduction for both pages and classes
      - I think there's value in extending this to fulltext or levenshtein distance-driven lookup/search, but I'm avoiding these for now as it takes some effort to enable them.
    */
    "def",
    "CREATE VIEW IF NOT EXISTS def (\n"
      "\t-- Combined summary of all -def types for easier joins.\n"
      "\trowid,\n"
      "\trefid,\n"
      "\tkind,\n"
      "\tname,\n"
      "\tsummary"
    ")\n"
    "as SELECT \n"
      "\trefid.rowid,\n"
      "\trefid.refid,\n"
      "\tmemberdef.kind,\n"
      "\tmemberdef.name,\n"
      "\tmemberdef.briefdescription \n"
    "FROM refid \n"
    "JOIN memberdef ON refid.rowid=memberdef.rowid \n"
    "UNION ALL \n"
    "SELECT \n"
      "\trefid.rowid,\n"
      "\trefid.refid,\n"
      "\tcompounddef.kind,\n"
      "\tcompounddef.name,\n"
      "\tCASE \n"
        "\t\tWHEN briefdescription IS NOT NULL \n"
        "\t\tTHEN briefdescription \n"
        "\t\tELSE title \n"
      "\tEND summary\n"
    "FROM refid \n"
    "JOIN compounddef ON refid.rowid=compounddef.rowid;"
  },
  {
    "inline_xrefs",
    "CREATE VIEW IF NOT EXISTS inline_xrefs (\n"
      "\t-- Crossrefs from inline member source.\n"
      "\trowid,\n"
      "\tsrc_rowid,\n"
      "\tdst_rowid\n"
    ")\n"
    "as SELECT \n"
      "\txrefs.rowid,\n"
      "\txrefs.src_rowid,\n"
      "\txrefs.dst_rowid\n"
    "FROM xrefs where xrefs.context='inline';\n"
  },
  {
    "argument_xrefs",
    "CREATE VIEW IF NOT EXISTS argument_xrefs (\n"
      "\t-- Crossrefs from member def/decl arguments\n"
      "\trowid,\n"
      "\tsrc_rowid,\n"
      "\tdst_rowid\n"
    ")\n"
    "as SELECT \n"
      "\txrefs.rowid,\n"
      "\txrefs.src_rowid,\n"
      "\txrefs.dst_rowid\n"
    "FROM xrefs where xrefs.context='argument';\n"
  },
  {
    "initializer_xrefs",
    "CREATE VIEW IF NOT EXISTS initializer_xrefs (\n"
      "\t-- Crossrefs from member initializers\n"
      "\trowid,\n"
      "\tsrc_rowid,\n"
      "\tdst_rowid\n"
    ")\n"
    "as SELECT \n"
      "\txrefs.rowid,\n"
      "\txrefs.src_rowid,\n"
      "\txrefs.dst_rowid\n"
    "FROM xrefs where xrefs.context='initializer';\n"
  },
  {
    "inner_outer",
    "CREATE VIEW IF NOT EXISTS inner_outer\n"
    "\t-- Joins 'contains' relations to simplify inner/outer 'rel' queries.\n"
    "as SELECT \n"
      "\tinner.*,\n"
      "\touter.*\n"
    "FROM def as inner\n"
      "\tjoin contains on inner.rowid=contains.inner_rowid\n"
      "\tjoin def as outer on outer.rowid=contains.outer_rowid;\n"
  },
  {
    "rel",
    "CREATE VIEW IF NOT EXISTS rel (\n"
      "\t-- Boolean indicator of relations available for a given entity.\n"
      "\t-- Join to (compound-|member-)def to find fetch-worthy relations.\n"
      "\trowid,\n"
      "\treimplemented,\n"
      "\treimplements,\n"
      "\tinnercompounds,\n"
      "\toutercompounds,\n"
      "\tinnerpages,\n"
      "\touterpages,\n"
      "\tinnerdirs,\n"
      "\touterdirs,\n"
      "\tinnerfiles,\n"
      "\touterfiles,\n"
      "\tinnerclasses,\n"
      "\touterclasses,\n"
      "\tinnernamespaces,\n"
      "\touternamespaces,\n"
      "\tinnergroups,\n"
      "\toutergroups,\n"
      "\tmembers,\n"
      "\tcompounds,\n"
      "\tsubclasses,\n"
      "\tsuperclasses,\n"
      "\tlinks_in,\n"
      "\tlinks_out,\n"
      "\targument_links_in,\n"
      "\targument_links_out,\n"
      "\tinitializer_links_in,\n"
      "\tinitializer_links_out\n"
    ")\n"
    "as SELECT \n"
      "\tdef.rowid,\n"
      "\tEXISTS (SELECT rowid FROM reimplements WHERE reimplemented_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM reimplements WHERE memberdef_rowid=def.rowid),\n"
      "\t-- rowid/kind for inner, [rowid:1/kind:1] for outer\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid AND kind='page'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid AND [kind:1]='page'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid AND kind='dir'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid AND [kind:1]='dir'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid AND kind='file'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid AND [kind:1]='file'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid AND kind in ('class', 'struct')),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid AND [kind:1] in ('class', 'struct')),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid AND kind='namespace'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid AND [kind:1]='namespace'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE [rowid:1]=def.rowid AND kind='group'),\n"
      "\tEXISTS (SELECT * FROM inner_outer WHERE rowid=def.rowid AND [kind:1]='group'),\n"
      "\tEXISTS (SELECT rowid FROM member WHERE scope_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM member WHERE memberdef_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM compoundref WHERE base_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM compoundref WHERE derived_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM inline_xrefs WHERE dst_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM inline_xrefs WHERE src_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM argument_xrefs WHERE dst_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM argument_xrefs WHERE src_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM initializer_xrefs WHERE dst_rowid=def.rowid),\n"
      "\tEXISTS (SELECT rowid FROM initializer_xrefs WHERE src_rowid=def.rowid)\n"
    "FROM def ORDER BY def.rowid;"
  }
};

//////////////////////////////////////////////////////
struct SqlStmt {
  const char   *query;
  sqlite3_stmt *stmt;
  sqlite3 *db;
};
//////////////////////////////////////////////////////
/* If you add a new statement below, make sure to add it to
   prepareStatements(). If sqlite3 is segfaulting (especially in
   sqlite3_clear_bindings()), using an un-prepared statement may
   be the cause. */
SqlStmt meta_insert = {
  "INSERT INTO meta "
    "( doxygen_version, schema_version, generated_at, generated_on, project_name, project_number, project_brief ) "
  "VALUES "
    "(:doxygen_version,:schema_version,:generated_at,:generated_on,:project_name,:project_number,:project_brief )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt incl_insert = {
  "INSERT INTO includes "
    "( local, src_id, dst_id ) "
  "VALUES "
    "(:local,:src_id,:dst_id )"
  ,NULL
};
SqlStmt incl_select = {
  "SELECT COUNT(*) FROM includes WHERE "
  "local=:local AND src_id=:src_id AND dst_id=:dst_id"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt contains_insert={
  "INSERT INTO contains "
    "( inner_rowid, outer_rowid )"
  "VALUES "
    "(:inner_rowid,:outer_rowid )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt file_select = {
  "SELECT rowid FROM file WHERE name=:name"
  ,NULL
};
SqlStmt file_insert = {
  "INSERT INTO file "
    "( name )"
  "VALUES "
    "(:name )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt refid_select =  {
  "SELECT rowid FROM refid WHERE refid=:refid"
  ,NULL
};
SqlStmt refid_insert = {
  "INSERT INTO refid "
    "( refid )"
  "VALUES "
    "(:refid )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt xrefs_insert= {
  "INSERT INTO xrefs "
    "( src_rowid, dst_rowid, context )"
  "VALUES "
    "(:src_rowid,:dst_rowid,:context )"
  ,NULL
};//////////////////////////////////////////////////////
SqlStmt reimplements_insert= {
  "INSERT INTO reimplements "
    "( memberdef_rowid, reimplemented_rowid )"
  "VALUES "
    "(:memberdef_rowid,:reimplemented_rowid )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt memberdef_exists={
  "SELECT EXISTS (SELECT * FROM memberdef WHERE rowid = :rowid)"
  ,NULL
};

SqlStmt memberdef_incomplete={
  "SELECT EXISTS ("
    "SELECT * FROM memberdef WHERE "
    "rowid = :rowid AND inline != 2 AND inline != :new_inline"
  ")"
  ,NULL
};

SqlStmt memberdef_insert={
  "INSERT INTO memberdef "
  "("
    "rowid,"
    "name,"
    "definition,"
    "type,"
    "argsstring,"
    "scope,"
    "initializer,"
    "bitfield,"
    "read,"
    "write,"
    "prot,"
    "static,"
    "const,"
    "explicit,"
    "inline,"
    "final,"
    "sealed,"
    "new,"
    "optional,"
    "required,"
    "volatile,"
    "virt,"
    "mutable,"
    "initonly,"
    "attribute,"
    "property,"
    "readonly,"
    "bound,"
    "constrained,"
    "transient,"
    "maybevoid,"
    "maybedefault,"
    "maybeambiguous,"
    "readable,"
    "writable,"
    "gettable,"
    "protectedsettable,"
    "protectedgettable,"
    "settable,"
    "privatesettable,"
    "privategettable,"
    "accessor,"
    "addable,"
    "removable,"
    "raisable,"
    "kind,"
    "bodystart,"
    "bodyend,"
    "bodyfile_id,"
    "file_id,"
    "line,"
    "column,"
    "detaileddescription,"
    "briefdescription,"
    "inbodydescription"
  ")"
  "VALUES "
  "("
    ":rowid,"
    ":name,"
    ":definition,"
    ":type,"
    ":argsstring,"
    ":scope,"
    ":initializer,"
    ":bitfield,"
    ":read,"
    ":write,"
    ":prot,"
    ":static,"
    ":const,"
    ":explicit,"
    ":inline,"
    ":final,"
    ":sealed,"
    ":new,"
    ":optional,"
    ":required,"
    ":volatile,"
    ":virt,"
    ":mutable,"
    ":initonly,"
    ":attribute,"
    ":property,"
    ":readonly,"
    ":bound,"
    ":constrained,"
    ":transient,"
    ":maybevoid,"
    ":maybedefault,"
    ":maybeambiguous,"
    ":readable,"
    ":writable,"
    ":gettable,"
    ":protectedsettable,"
    ":protectedgettable,"
    ":settable,"
    ":privatesettable,"
    ":privategettable,"
    ":accessor,"
    ":addable,"
    ":removable,"
    ":raisable,"
    ":kind,"
    ":bodystart,"
    ":bodyend,"
    ":bodyfile_id,"
    ":file_id,"
    ":line,"
    ":column,"
    ":detaileddescription,"
    ":briefdescription,"
    ":inbodydescription"
  ")"
  ,NULL
};
/* We have a slightly different need than the XML here. The XML can have two memberdef nodes with the same refid to document the declaration and the definition. This doesn't play very nice with a referential model. It isn't a big issue if only one is documented, but in case both are, we'll fall back on this kludge to combine them in a single row... */
SqlStmt memberdef_update_decl={
  "UPDATE memberdef SET "
    "inline = :inline,"
    "file_id = :file_id,"
    "line = :line,"
    "column = :column,"
    "detaileddescription = 'Declaration: ' || :detaileddescription || 'Definition: ' || detaileddescription,"
    "briefdescription = 'Declaration: ' || :briefdescription || 'Definition: ' || briefdescription,"
    "inbodydescription = 'Declaration: ' || :inbodydescription || 'Definition: ' || inbodydescription "
  "WHERE rowid = :rowid"
  ,NULL
};
SqlStmt memberdef_update_def={
  "UPDATE memberdef SET "
    "inline = :inline,"
    "bodystart = :bodystart,"
    "bodyend = :bodyend,"
    "bodyfile_id = :bodyfile_id,"
    "detaileddescription = 'Declaration: ' || detaileddescription || 'Definition: ' || :detaileddescription,"
    "briefdescription = 'Declaration: ' || briefdescription || 'Definition: ' || :briefdescription,"
    "inbodydescription = 'Declaration: ' || inbodydescription || 'Definition: ' || :inbodydescription "
  "WHERE rowid = :rowid"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt member_insert={
  "INSERT INTO member "
    "( scope_rowid, memberdef_rowid, prot, virt, ambiguityscope ) "
  "VALUES "
    "(:scope_rowid,:memberdef_rowid,:prot,:virt,:ambiguityscope )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt compounddef_insert={
  "INSERT INTO compounddef "
  "("
    "rowid,"
    "name,"
    "title,"
    "kind,"
    "prot,"
    "file_id,"
    "line,"
    "column,"
    "briefdescription,"
    "detaileddescription"
  ")"
  "VALUES "
  "("
    ":rowid,"
    ":name,"
    ":title,"
    ":kind,"
    ":prot,"
    ":file_id,"
    ":line,"
    ":column,"
    ":briefdescription,"
    ":detaileddescription"
  ")"
  ,NULL
};
SqlStmt compounddef_exists={
  "SELECT EXISTS ("
    "SELECT * FROM compounddef WHERE rowid = :rowid"
  ")"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt compoundref_insert={
  "INSERT INTO compoundref "
    "( base_rowid, derived_rowid, prot, virt ) "
  "VALUES "
    "(:base_rowid,:derived_rowid,:prot,:virt )"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt param_select = {
  "SELECT rowid FROM param WHERE "
    "(attributes IS NULL OR attributes=:attributes) AND "
    "(type IS NULL OR type=:type) AND "
    "(declname IS NULL OR declname=:declname) AND "
    "(defname IS NULL OR defname=:defname) AND "
    "(array IS NULL OR array=:array) AND "
    "(defval IS NULL OR defval=:defval) AND "
    "(briefdescription IS NULL OR briefdescription=:briefdescription)"
  ,NULL
};
SqlStmt param_insert = {
  "INSERT INTO param "
    "( attributes, type, declname, defname, array, defval, briefdescription ) "
  "VALUES "
    "(:attributes,:type,:declname,:defname,:array,:defval,:briefdescription)"
  ,NULL
};
//////////////////////////////////////////////////////
SqlStmt memberdef_param_insert={
  "INSERT INTO memberdef_param "
    "( memberdef_id, param_id)"
  "VALUES "
    "(:memberdef_id,:param_id)"
  ,NULL
};


class TextGeneratorSqlite3Impl : public TextGeneratorIntf
{
  public:
    TextGeneratorSqlite3Impl(StringList &l) : l(l) {
      l.setAutoDelete(TRUE);
    }
    void writeString(const char * /*s*/,bool /*keepSpaces*/) const
    {
    }
    void writeBreak(int) const
    {
      DBG_CTX(("writeBreak\n"));
    }
    void writeLink(const char * /*extRef*/,const char *file,
                   const char *anchor,const char * /*text*/
                  ) const
    {
      QCString *rs=new QCString(file);
      if (anchor)
      {
        rs->append("_1").append(anchor);
      }
      l.append(rs);
    }
  private:
    StringList &l;
    // the list is filled by linkifyText and consumed by the caller
};


static bool bindTextParameter(SqlStmt &s,const char *name,const char *value, bool _static=TRUE)
{
  int idx = sqlite3_bind_parameter_index(s.stmt, name);
  if (idx==0) {
    msg("sqlite3_bind_parameter_index(%s)[%s] failed: %s\n", name, s.query, sqlite3_errmsg(s.db));
    return false;
  }
  int rv = sqlite3_bind_text(s.stmt, idx, value, -1, _static==TRUE?SQLITE_STATIC:SQLITE_TRANSIENT);
  if (rv!=SQLITE_OK) {
    msg("sqlite3_bind_text(%s)[%s] failed: %s\n", name, s.query, sqlite3_errmsg(s.db));
    return false;
  }
  return true;
}

static bool bindIntParameter(SqlStmt &s,const char *name,int value)
{
  int idx = sqlite3_bind_parameter_index(s.stmt, name);
  if (idx==0) {
    msg("sqlite3_bind_parameter_index(%s)[%s] failed to find column: %s\n", name, s.query, sqlite3_errmsg(s.db));
    return false;
  }
  int rv = sqlite3_bind_int(s.stmt, idx, value);
  if (rv!=SQLITE_OK) {
    msg("sqlite3_bind_int(%s)[%s] failed: %s\n", name, s.query, sqlite3_errmsg(s.db));
    return false;
  }
  return true;
}

static int step(SqlStmt &s,bool getRowId=FALSE, bool select=FALSE)
{
  int rowid=-1;
  int rc = sqlite3_step(s.stmt);
  if (rc!=SQLITE_DONE && rc!=SQLITE_ROW)
  {
    msg("sqlite3_step: %s (rc: %d)\n", sqlite3_errmsg(s.db), rc);
    sqlite3_reset(s.stmt);
    sqlite3_clear_bindings(s.stmt);
    return -1;
  }
  if (getRowId && select) rowid = sqlite3_column_int(s.stmt, 0); // works on selects, doesn't on inserts
  if (getRowId && !select) rowid = sqlite3_last_insert_rowid(s.db); //works on inserts, doesn't on selects
  sqlite3_reset(s.stmt);
  sqlite3_clear_bindings(s.stmt); // XXX When should this really be called
  return rowid;
}

static int insertFile(const char* name)
{
  int rowid=-1;
  if (name==0) return rowid;

  name = stripFromPath(name);

  bindTextParameter(file_select,":name",name,FALSE);
  rowid=step(file_select,TRUE,TRUE);
  if (rowid==0)
  {
    bindTextParameter(file_insert,":name",name,FALSE);
    rowid=step(file_insert,TRUE);
  }
  return rowid;
}

static void recordMetadata()
{
  bindTextParameter(meta_insert,":doxygen_version",versionString);
  bindTextParameter(meta_insert,":schema_version","0.2.0"); //TODO: this should be a constant somewhere; not sure where
  bindTextParameter(meta_insert,":generated_at",dateToString(TRUE), FALSE);
  bindTextParameter(meta_insert,":generated_on",dateToString(FALSE), FALSE);
  bindTextParameter(meta_insert,":project_name",Config_getString(PROJECT_NAME));
  bindTextParameter(meta_insert,":project_number",Config_getString(PROJECT_NUMBER));
  bindTextParameter(meta_insert,":project_brief",Config_getString(PROJECT_BRIEF));
  step(meta_insert);
}

struct Refid {
  int rowid;
  const char *refid;
  bool created;
};

struct Refid insertRefid(const char *refid)
{
  struct Refid ret;
  ret.rowid=-1;
  ret.refid=refid;
  ret.created = FALSE;
  if (refid==0) return ret;

  bindTextParameter(refid_select,":refid",refid);
  ret.rowid=step(refid_select,TRUE,TRUE);
  if (ret.rowid==0)
  {
    bindTextParameter(refid_insert,":refid",refid);
    ret.rowid=step(refid_insert,TRUE);
    ret.created = TRUE;
  }

  return ret;
}

static bool memberdefExists(struct Refid refid)
{
  bindIntParameter(memberdef_exists,":rowid",refid.rowid);
  int test = step(memberdef_exists,TRUE,TRUE);
  return test ? true : false;
}

static bool memberdefIncomplete(struct Refid refid, const MemberDef* md)
{
  bindIntParameter(memberdef_incomplete,":rowid",refid.rowid);
  bindIntParameter(memberdef_incomplete,":new_inline",md->isInline());
  int test = step(memberdef_incomplete,TRUE,TRUE);
  return test ? true : false;
}

static bool compounddefExists(struct Refid refid)
{
  bindIntParameter(compounddef_exists,":rowid",refid.rowid);
  int test = step(compounddef_exists,TRUE,TRUE);
  return test ? true : false;
}

static bool insertMemberReference(struct Refid src_refid, struct Refid dst_refid, const char *context)
{
  if (src_refid.rowid==-1||dst_refid.rowid==-1)
    return false;

  if (
     !bindIntParameter(xrefs_insert,":src_rowid",src_refid.rowid) ||
     !bindIntParameter(xrefs_insert,":dst_rowid",dst_refid.rowid)
     )
  {
    return false;
  }
  else
  {
    bindTextParameter(xrefs_insert,":context",context);
  }

  step(xrefs_insert);
  return true;
}

static void insertMemberReference(const MemberDef *src, const MemberDef *dst, const char *context)
{
  QCString qdst_refid = dst->getOutputFileBase() + "_1" + dst->anchor();
  QCString qsrc_refid = src->getOutputFileBase() + "_1" + src->anchor();

  struct Refid src_refid = insertRefid(qsrc_refid);
  struct Refid dst_refid = insertRefid(qdst_refid);
  insertMemberReference(src_refid,dst_refid,context);
}

static void insertMemberFunctionParams(int memberdef_id, const MemberDef *md, const Definition *def)
{
  ArgumentList *declAl = md->declArgumentList();
  ArgumentList *defAl = md->argumentList();
  if (declAl!=0 && declAl->count()>0)
  {
    ArgumentListIterator declAli(*declAl);
    ArgumentListIterator defAli(*defAl);
    Argument *a;
    for (declAli.toFirst();(a=declAli.current());++declAli)
    {
      Argument *defArg = defAli.current();

      if (!a->attrib.isEmpty())
      {
        bindTextParameter(param_select,":attributes",a->attrib);
        bindTextParameter(param_insert,":attributes",a->attrib);
      }
      if (!a->type.isEmpty())
      {
        StringList l;
        linkifyText(TextGeneratorSqlite3Impl(l),def,md->getBodyDef(),md,a->type);

        StringListIterator li(l);
        QCString *s;
        while ((s=li.current()))
        {
          QCString qsrc_refid = md->getOutputFileBase() + "_1" + md->anchor();
          struct Refid src_refid = insertRefid(qsrc_refid);
          struct Refid dst_refid = insertRefid(s->data());
          insertMemberReference(src_refid,dst_refid, "argument");
          ++li;
        }
        bindTextParameter(param_select,":type",a->type);
        bindTextParameter(param_insert,":type",a->type);
      }
      if (!a->name.isEmpty())
      {
        bindTextParameter(param_select,":declname",a->name);
        bindTextParameter(param_insert,":declname",a->name);
      }
      if (defArg && !defArg->name.isEmpty() && defArg->name!=a->name)
      {
        bindTextParameter(param_select,":defname",defArg->name);
        bindTextParameter(param_insert,":defname",defArg->name);
      }
      if (!a->array.isEmpty())
      {
        bindTextParameter(param_select,":array",a->array);
        bindTextParameter(param_insert,":array",a->array);
      }
      if (!a->defval.isEmpty())
      {
        StringList l;
        linkifyText(TextGeneratorSqlite3Impl(l),def,md->getBodyDef(),md,a->defval);
        bindTextParameter(param_select,":defval",a->defval);
        bindTextParameter(param_insert,":defval",a->defval);
      }
      if (defArg) ++defAli;

      int param_id=step(param_select,TRUE,TRUE);
      if (param_id==0) {
        param_id=step(param_insert,TRUE);
      }
      if (param_id==-1) {
          msg("error INSERT params failed\n");
          continue;
      }

      bindIntParameter(memberdef_param_insert,":memberdef_id",memberdef_id);
      bindIntParameter(memberdef_param_insert,":param_id",param_id);
      step(memberdef_param_insert);
    }
  }
}

static void insertMemberDefineParams(int memberdef_id,const MemberDef *md, const Definition *def)
{
    if (md->argumentList()->count()==0) // special case for "foo()" to
                                        // disguish it from "foo".
    {
      DBG_CTX(("no params\n"));
    }
    else
    {
      ArgumentListIterator ali(*md->argumentList());
      Argument *a;
      for (ali.toFirst();(a=ali.current());++ali)
      {
        bindTextParameter(param_insert,":defname",a->type);
        int param_id=step(param_insert,TRUE);
        if (param_id==-1) {
          msg("error INSERT param(%s) failed\n", a->type.data());
          continue;
        }

        bindIntParameter(memberdef_param_insert,":memberdef_id",memberdef_id);
        bindIntParameter(memberdef_param_insert,":param_id",param_id);
        step(memberdef_param_insert);
      }
    }
}


static void stripQualifiers(QCString &typeStr)
{
  bool done=FALSE;
  while (!done)
  {
    if      (typeStr.stripPrefix("static "));
    else if (typeStr.stripPrefix("virtual "));
    else if (typeStr.stripPrefix("volatile "));
    else if (typeStr=="virtual") typeStr="";
    else done=TRUE;
  }
}

static int prepareStatement(sqlite3 *db, SqlStmt &s)
{
  int rc;
  rc = sqlite3_prepare_v2(db,s.query,-1,&s.stmt,0);
  if (rc!=SQLITE_OK)
  {
    msg("prepare failed for %s\n%s\n", s.query, sqlite3_errmsg(db));
    s.db = NULL;
    return -1;
  }
  s.db = db;
  return rc;
}

static int prepareStatements(sqlite3 *db)
{
  if (
  -1==prepareStatement(db, meta_insert) ||
  -1==prepareStatement(db, memberdef_exists) ||
  -1==prepareStatement(db, memberdef_incomplete) ||
  -1==prepareStatement(db, memberdef_insert) ||
  -1==prepareStatement(db, memberdef_update_def) ||
  -1==prepareStatement(db, memberdef_update_decl) ||
  -1==prepareStatement(db, member_insert) ||
  -1==prepareStatement(db, file_insert) ||
  -1==prepareStatement(db, file_select) ||
  -1==prepareStatement(db, refid_insert) ||
  -1==prepareStatement(db, refid_select) ||
  -1==prepareStatement(db, incl_insert)||
  -1==prepareStatement(db, incl_select)||
  -1==prepareStatement(db, param_insert) ||
  -1==prepareStatement(db, param_select) ||
  -1==prepareStatement(db, xrefs_insert) ||
  -1==prepareStatement(db, reimplements_insert) ||
  -1==prepareStatement(db, contains_insert) ||
  -1==prepareStatement(db, compounddef_exists) ||
  -1==prepareStatement(db, compounddef_insert) ||
  -1==prepareStatement(db, compoundref_insert) ||
  -1==prepareStatement(db, memberdef_param_insert)
  )
  {
    return -1;
  }
  return 0;
}

static void beginTransaction(sqlite3 *db)
{
  char * sErrMsg = 0;
  sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &sErrMsg);
}

static void endTransaction(sqlite3 *db)
{
  char * sErrMsg = 0;
  sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sErrMsg);
}

static void pragmaTuning(sqlite3 *db)
{
  char * sErrMsg = 0;
  sqlite3_exec(db, "PRAGMA synchronous = OFF", NULL, NULL, &sErrMsg);
  sqlite3_exec(db, "PRAGMA journal_mode = MEMORY", NULL, NULL, &sErrMsg);
  sqlite3_exec(db, "PRAGMA temp_store = MEMORY;", NULL, NULL, &sErrMsg);
}

static int initializeTables(sqlite3* db)
{
  int rc;
  sqlite3_stmt *stmt = 0;

  msg("Initializing DB schema (tables)...\n");
  for (unsigned int k = 0; k < sizeof(table_schema) / sizeof(table_schema[0]); k++)
  {
    const char *q = table_schema[k][1];
    char *errmsg;
    rc = sqlite3_exec(db, q, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
      msg("failed to execute query: %s\n\t%s\n", q, errmsg);
      return -1;
    }
  }
  return 0;
}

static int initializeViews(sqlite3* db)
{
  int rc;
  sqlite3_stmt *stmt = 0;

  msg("Initializing DB schema (views)...\n");
  for (unsigned int k = 0; k < sizeof(view_schema) / sizeof(view_schema[0]); k++)
  {
    const char *q = view_schema[k][1];
    char *errmsg;
    rc = sqlite3_exec(db, q, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
      msg("failed to execute query: %s\n\t%s\n", q, errmsg);
      return -1;
    }
  }
  return 0;
}

////////////////////////////////////////////
// TODO: I collapsed all innerX tables into 'contains', which raises the prospect that all of these very similar writeInnerX methods could be refactored into a single method, or a small set of common parts.
static void writeInnerClasses(const ClassSDict *cl, struct Refid outer_refid)
{
  if (!cl) return;

  ClassSDict::Iterator cli(*cl);
  const ClassDef *cd;
  for (cli.toFirst();(cd=cli.current());++cli)
  {
    if (!cd->isHidden() && cd->name().find('@')==-1) // skip anonymous scopes
    {
      struct Refid inner_refid = insertRefid(cd->getOutputFileBase());

      bindIntParameter(contains_insert,":inner_rowid", inner_refid.rowid);
      bindIntParameter(contains_insert,":outer_rowid", outer_refid.rowid);
      step(contains_insert);
    }
  }
}

static void writeInnerPages(const PageSDict *pl, struct Refid outer_refid)
{
  if (!pl) return;

  PageSDict::Iterator pli(*pl);
  const PageDef *pd;
  for (pli.toFirst();(pd=pli.current());++pli)
  {
    struct Refid inner_refid = insertRefid(pd->getGroupDef() ? pd->getOutputFileBase()+"_"+pd->name() : pd->getOutputFileBase());

    bindIntParameter(contains_insert,":inner_rowid", inner_refid.rowid);
    bindIntParameter(contains_insert,":outer_rowid", outer_refid.rowid);
    step(contains_insert);

  }
}

static void writeInnerGroups(const GroupList *gl, struct Refid outer_refid)
{
  if (gl)
  {
    GroupListIterator gli(*gl);
    const GroupDef *sgd;
    for (gli.toFirst();(sgd=gli.current());++gli)
    {
      struct Refid inner_refid = insertRefid(sgd->getOutputFileBase());

      bindIntParameter(contains_insert,":inner_rowid", inner_refid.rowid);
      bindIntParameter(contains_insert,":outer_rowid", outer_refid.rowid);
      step(contains_insert);
    }
  }
}

static void writeInnerFiles(const FileList *fl, struct Refid outer_refid)
{
  if (fl)
  {
    QListIterator<FileDef> fli(*fl);
    const FileDef *fd;
    for (fli.toFirst();(fd=fli.current());++fli)
    {
      struct Refid inner_refid = insertRefid(fd->getOutputFileBase());

      bindIntParameter(contains_insert,":inner_rowid", inner_refid.rowid);
      bindIntParameter(contains_insert,":outer_rowid", outer_refid.rowid);
      step(contains_insert);
    }
  }
}

static void writeInnerDirs(const DirList *dl, struct Refid outer_refid)
{
  if (dl)
  {
    QListIterator<DirDef> subdirs(*dl);
    const DirDef *subdir;
    for (subdirs.toFirst();(subdir=subdirs.current());++subdirs)
    {
      struct Refid inner_refid = insertRefid(subdir->getOutputFileBase());

      bindIntParameter(contains_insert,":inner_rowid", inner_refid.rowid);
      bindIntParameter(contains_insert,":outer_rowid", outer_refid.rowid);
      step(contains_insert);
    }
  }
}

static void writeInnerNamespaces(const NamespaceSDict *nl, struct Refid outer_refid)
{
  if (nl)
  {
    NamespaceSDict::Iterator nli(*nl);
    const NamespaceDef *nd;
    for (nli.toFirst();(nd=nli.current());++nli)
    {
      if (!nd->isHidden() && nd->name().find('@')==-1) // skip anonymous scopes
      {
        struct Refid inner_refid = insertRefid(nd->getOutputFileBase());

        bindIntParameter(contains_insert,":inner_rowid",inner_refid.rowid);
        bindIntParameter(contains_insert,":outer_rowid",outer_refid.rowid);
        step(contains_insert);
      }
    }
  }
}


static void writeTemplateArgumentList(const ArgumentList * al,
                                      const Definition * scope,
                                      const FileDef * fileScope)
{
  if (al)
  {
    ArgumentListIterator ali(*al);
    Argument *a;
    for (ali.toFirst();(a=ali.current());++ali)
    {
      if (!a->type.isEmpty())
      {
        #warning linkifyText(TextGeneratorXMLImpl(t),scope,fileScope,0,a->type);
        bindTextParameter(param_select,":type",a->type);
        bindTextParameter(param_insert,":type",a->type);
      }
      if (!a->name.isEmpty())
      {
        bindTextParameter(param_select,":declname",a->name);
        bindTextParameter(param_insert,":declname",a->name);
        bindTextParameter(param_select,":defname",a->name);
        bindTextParameter(param_insert,":defname",a->name);
      }
      if (!a->defval.isEmpty())
      {
        #warning linkifyText(TextGeneratorXMLImpl(t),scope,fileScope,0,a->defval);
        bindTextParameter(param_select,":defval",a->defval);
        bindTextParameter(param_insert,":defval",a->defval);
      }
      if (!step(param_select,TRUE,TRUE))
        step(param_insert);
    }
  }
}

static void writeMemberTemplateLists(const MemberDef *md)
{
  ArgumentList *templMd = md->templateArguments();
  if (templMd) // function template prefix
  {
    writeTemplateArgumentList(templMd,md->getClassDef(),md->getFileDef());
  }
}
static void writeTemplateList(const ClassDef *cd)
{
  writeTemplateArgumentList(cd->templateArguments(),cd,0);
}

QCString getSQLDocBlock(const Definition *scope,
  const Definition *def,
  const QCString &doc,
  const QCString &fileName,
  int lineNr)
{
  QGString s;
  if (doc.isEmpty()) return s.data();
  FTextStream t(&s);
  DocNode *root = validatingParseDoc(fileName,lineNr,const_cast<Definition*>(scope),const_cast<MemberDef*>(reinterpret_cast<const MemberDef*>(def)),doc,FALSE,FALSE);
  XMLCodeGenerator codeGen(t);
  // create a parse tree visitor for XML
  XmlDocVisitor *visitor = new XmlDocVisitor(t,codeGen);
  root->accept(visitor);
  delete visitor;
  delete root;
  QCString result = convertCharEntitiesToUTF8(s.data());
  return result.data();
}

static void getSQLDesc(SqlStmt &s,const char *col,const char *value,const Definition *def)
{
  bindTextParameter(
    s,
    col,
      getSQLDocBlock(
        def->getOuterScope(),
        def,
        value,
        def->docFile(),
        def->docLine()
      ),
      FALSE
    );
}
////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
static void generateSqlite3ForMember(const MemberDef *md, const Definition *def)
{
  // + declaration/definition arg lists
  // + reimplements
  // + reimplementedBy
  // - exceptions
  // + const/volatile specifiers
  // - examples
  // + source definition
  // + source references
  // + source referenced by
  // - body code
  // + template arguments
  //     (templateArguments(), definitionTemplateParameterLists())
  // - call graph

  // enum values are written as part of the enum
  if (md->memberType()==MemberType_EnumValue) return;
  if (md->isHidden()) return;
  //if (md->name().at(0)=='@') return; // anonymous member

  // group members are only visible in their group
  //if (def->definitionType()!=Definition::TypeGroup && md->getGroupDef()) return;
  QCString memType;

  // memberdef
  QCString qrefid = md->getOutputFileBase() + "_1" + md->anchor();
  struct Refid refid = insertRefid(qrefid);

  /* TODO: not 100% certain this is safe for all memberdef types */
  if(!refid.created && memberdefExists(refid) && memberdefIncomplete(refid, md)) // compacting duplicate defs
  {
    /*
    NOTE: For performance, ideal condition is that we *don't process* a member we've alread added. Unfortunately, we can have two memberdefs with the same refid documenting the declaration and definition. The old process, because of this hitch, would "update" an already-complete member when different inheritance paths lead back to the same refid. memberdefIncomplete uses the 'inline' value to figure this out. Once we get to this process, we should *only* be seeing the *other* type of def/decl, so we'll set inline to a new value (2), indicating that this entry covers both inline types.
    */
    struct SqlStmt memberdef_update;

    // definitions have bodyfile/start/end
    if (md->getStartBodyLine()!=-1)
    {
      memberdef_update = memberdef_update_def;
      int bodyfile_id = insertFile(md->getBodyDef()->absFilePath());
      if (bodyfile_id == -1)
      {
          sqlite3_clear_bindings(memberdef_update.stmt);
      }
      else
      {
          bindIntParameter(memberdef_update,":bodyfile_id",bodyfile_id);
          bindIntParameter(memberdef_update,":bodystart",md->getStartBodyLine());
          bindIntParameter(memberdef_update,":bodyend",md->getEndBodyLine());
      }
    }
    // declarations don't
    else
    {
      memberdef_update = memberdef_update_decl;
      if (md->getDefLine() != -1)
      {
        int file_id = insertFile(md->getDefFileName());
        if (file_id!=-1)
        {
          bindIntParameter(memberdef_update,":file_id",file_id);
          bindIntParameter(memberdef_update,":line",md->getDefLine());
          bindIntParameter(memberdef_update,":column",md->getDefColumn());
        }
      }
    }

    bindIntParameter(memberdef_update, ":rowid", refid.rowid);
    // value 2 indicates we've seen "both" inline types.
    bindIntParameter(memberdef_update,":inline", 2);

    /* in case both are used, append/prepend descriptions */
    getSQLDesc(memberdef_update,":briefdescription",md->briefDescription(),md);
    getSQLDesc(memberdef_update,":detaileddescription",md->documentation(),md);
    getSQLDesc(memberdef_update,":inbodydescription",md->inbodyDocumentation(),md);

    step(memberdef_update,TRUE);

    // don't think we need to repeat params; should have from first encounter
    // TODO: make sure a test-case in the docset illustrates that this works on overloaded functions with more than one signature (I assume they'll have different refids and that it won't matter)

    // + source references
    // The cross-references in initializers only work when both the src and dst
    // are defined.
    MemberSDict *mdict = md->getReferencesMembers();
    if (mdict!=0)
    {
      MemberSDict::IteratorDict mdi(*mdict);
      const MemberDef *rmd;
      for (mdi.toFirst();(rmd=mdi.current());++mdi)
      {
        insertMemberReference(md,rmd, "inline");
      }
    }
    // + source referenced by
    mdict = md->getReferencedByMembers();
    if (mdict!=0)
    {
      MemberSDict::IteratorDict mdi(*mdict);
      const MemberDef *rmd;
      for (mdi.toFirst();(rmd=mdi.current());++mdi)
      {
        insertMemberReference(rmd,md, "inline");
      }
    }
    return;
  }

  bindIntParameter(memberdef_insert,":rowid", refid.rowid);
  bindTextParameter(memberdef_insert,":kind",md->memberTypeName(),FALSE);
  bindIntParameter(memberdef_insert,":prot",md->protection());

  bindIntParameter(memberdef_insert,":static",md->isStatic());

  bool isFunc=FALSE;
  switch (md->memberType())
  {
    case MemberType_Function: // fall through
    case MemberType_Signal:   // fall through
    case MemberType_Friend:   // fall through
    case MemberType_DCOP:     // fall through
    case MemberType_Slot:
      isFunc=TRUE;
      break;
    default:
      break;
  }

  if (isFunc)
  {
    ArgumentList *al = md->argumentList();
    if (al!=0)
    {
      bindIntParameter(memberdef_insert,":const",al->constSpecifier);
      bindIntParameter(memberdef_insert,":volatile",al->volatileSpecifier);
    }
    bindIntParameter(memberdef_insert,":explicit",md->isExplicit());
    bindIntParameter(memberdef_insert,":inline",md->isInline());
    bindIntParameter(memberdef_insert,":final",md->isFinal());
    bindIntParameter(memberdef_insert,":sealed",md->isSealed());
    bindIntParameter(memberdef_insert,":new",md->isNew());
    bindIntParameter(memberdef_insert,":optional",md->isOptional());
    bindIntParameter(memberdef_insert,":required",md->isRequired());

    bindIntParameter(memberdef_insert,":virt",md->virtualness());
  }

  if (md->memberType() == MemberType_Variable)
  {
    bindIntParameter(memberdef_insert,":mutable",md->isMutable());
    bindIntParameter(memberdef_insert,":initonly",md->isInitonly());
    bindIntParameter(memberdef_insert,":attribute",md->isAttribute());
    bindIntParameter(memberdef_insert,":property",md->isProperty());
    bindIntParameter(memberdef_insert,":readonly",md->isReadonly());
    bindIntParameter(memberdef_insert,":bound",md->isBound());
    bindIntParameter(memberdef_insert,":removable",md->isRemovable());
    bindIntParameter(memberdef_insert,":constrained",md->isConstrained());
    bindIntParameter(memberdef_insert,":transient",md->isTransient());
    bindIntParameter(memberdef_insert,":maybevoid",md->isMaybeVoid());
    bindIntParameter(memberdef_insert,":maybedefault",md->isMaybeDefault());
    bindIntParameter(memberdef_insert,":maybeambiguous",md->isMaybeAmbiguous());
    if (md->bitfieldString())
    {
      QCString bitfield = md->bitfieldString();
      if (bitfield.at(0)==':') bitfield=bitfield.mid(1);
      bindTextParameter(memberdef_insert,":bitfield",bitfield.stripWhiteSpace());
    }
  }
  else if (md->memberType() == MemberType_Property)
  {
    bindIntParameter(memberdef_insert,":readable",md->isReadable());
    bindIntParameter(memberdef_insert,":writable",md->isWritable());
    bindIntParameter(memberdef_insert,":gettable",md->isGettable());
    bindIntParameter(memberdef_insert,":privategettable",md->isPrivateGettable());
    bindIntParameter(memberdef_insert,":protectedgettable",md->isProtectedGettable());
    bindIntParameter(memberdef_insert,":settable",md->isSettable());
    bindIntParameter(memberdef_insert,":privatesettable",md->isPrivateSettable());
    bindIntParameter(memberdef_insert,":protectedsettable",md->isProtectedSettable());

    if (md->isAssign() || md->isCopy() || md->isRetain()
     || md->isStrong() || md->isWeak())
    {
      int accessor=0;
      if (md->isAssign())      accessor = 1;
      else if (md->isCopy())   accessor = 2;
      else if (md->isRetain()) accessor = 3;
      else if (md->isStrong()) accessor = 4;
      else if (md->isWeak())   accessor = 5;

      bindIntParameter(memberdef_insert,":accessor",accessor);
    }
    bindTextParameter(memberdef_insert,":read",md->getReadAccessor());
    bindTextParameter(memberdef_insert,":write",md->getWriteAccessor());
  }
  else if (md->memberType() == MemberType_Event)
  {
    bindIntParameter(memberdef_insert,":addable",md->isAddable());
    bindIntParameter(memberdef_insert,":removable",md->isRemovable());
    bindIntParameter(memberdef_insert,":raisable",md->isRaisable());
  }

  const MemberDef *rmd = md->reimplements();
  if(rmd)
  {
    QCString qreimplemented_refid = rmd->getOutputFileBase() + "_1" + rmd->anchor();

    struct Refid reimplemented_refid = insertRefid(qreimplemented_refid);

    bindIntParameter(reimplements_insert,":memberdef_rowid", refid.rowid);
    bindIntParameter(reimplements_insert,":reimplemented_rowid", reimplemented_refid.rowid);
    step(reimplements_insert,TRUE);
  }

  // + declaration/definition arg lists
  if (md->memberType()!=MemberType_Define &&
      md->memberType()!=MemberType_Enumeration
     )
  {
    if (md->memberType()!=MemberType_Typedef)
    {
      writeMemberTemplateLists(md);
    }
    QCString typeStr = md->typeString();
    stripQualifiers(typeStr);
    StringList l;
    linkifyText(TextGeneratorSqlite3Impl(l), def, md->getBodyDef(),md,typeStr);
    if (typeStr)
    {
      bindTextParameter(memberdef_insert,":type",typeStr,FALSE);
    }

    if (md->definition())
    {
      bindTextParameter(memberdef_insert,":definition",md->definition());
    }

    if (md->argsString())
    {
      bindTextParameter(memberdef_insert,":argsstring",md->argsString());
    }
  }

  bindTextParameter(memberdef_insert,":name",md->name());

  // Extract references from initializer
  if (md->hasMultiLineInitializer() || md->hasOneLineInitializer())
  {
    bindTextParameter(memberdef_insert,":initializer",md->initializer());

    StringList l;
    linkifyText(TextGeneratorSqlite3Impl(l),def,md->getBodyDef(),md,md->initializer());
    StringListIterator li(l);
    QCString *s;
    while ((s=li.current()))
    {
      if (md->getBodyDef())
      {
        DBG_CTX(("initializer:%s %s %s %d\n",
              md->anchor(),
              s->data(),
              md->getBodyDef()->getDefFileName(),
              md->getStartBodyLine()));
        QCString qsrc_refid = md->getOutputFileBase() + "_1" + md->anchor();
        struct Refid src_refid = insertRefid(qsrc_refid);
        struct Refid dst_refid = insertRefid(s->data());
        insertMemberReference(src_refid,dst_refid, "initializer");
      }
      ++li;
    }
  }

  if ( md->getScopeString() )
  {
    bindTextParameter(memberdef_insert,":scope",md->getScopeString(),FALSE);
  }

  // +Brief, detailed and inbody description
  getSQLDesc(memberdef_insert,":briefdescription",md->briefDescription(),md);
  getSQLDesc(memberdef_insert,":detaileddescription",md->documentation(),md);
  getSQLDesc(memberdef_insert,":inbodydescription",md->inbodyDocumentation(),md);

  // File location
  if (md->getDefLine() != -1)
  {
    int file_id = insertFile(md->getDefFileName());
    if (file_id!=-1)
    {
      bindIntParameter(memberdef_insert,":file_id",file_id);
      bindIntParameter(memberdef_insert,":line",md->getDefLine());
      bindIntParameter(memberdef_insert,":column",md->getDefColumn());

      // definitions also have bodyfile/start/end
      if (md->getStartBodyLine()!=-1)
      {
        int bodyfile_id = insertFile(md->getBodyDef()->absFilePath());
        if (bodyfile_id == -1)
        {
            sqlite3_clear_bindings(memberdef_insert.stmt);
        }
        else
        {
            bindIntParameter(memberdef_insert,":bodyfile_id",bodyfile_id);
            bindIntParameter(memberdef_insert,":bodystart",md->getStartBodyLine());
            bindIntParameter(memberdef_insert,":bodyend",md->getEndBodyLine());
        }
      }
    }
  }

  int memberdef_id=step(memberdef_insert,TRUE);

  if (isFunc)
  {
    insertMemberFunctionParams(memberdef_id,md,def);
  }
  else if (md->memberType()==MemberType_Define &&
          md->argsString())
  {
    insertMemberDefineParams(memberdef_id,md,def);
  }

  // + source references
  // The cross-references in initializers only work when both the src and dst
  // are defined.
  MemberSDict *mdict = md->getReferencesMembers();
  if (mdict!=0)
  {
    MemberSDict::IteratorDict mdi(*mdict);
    const MemberDef *rmd;
    for (mdi.toFirst();(rmd=mdi.current());++mdi)
    {
      insertMemberReference(md,rmd, "inline");
    }
  }
  // + source referenced by
  mdict = md->getReferencedByMembers();
  if (mdict!=0)
  {
    MemberSDict::IteratorDict mdi(*mdict);
    const  MemberDef *rmd;
    for (mdi.toFirst();(rmd=mdi.current());++mdi)
    {
      insertMemberReference(rmd,md, "inline");
    }
  }
}

static void generateSqlite3Section( const Definition *d,
                      const MemberList *ml,
                      const char * /*kind*/,
                      const char * /*header*/=0,
                      const char * /*documentation*/=0)
{
  if (ml==0) return;
  MemberListIterator mli(*ml);
  const MemberDef *md;

  for (mli.toFirst();(md=mli.current());++mli)
  {
    // namespace members are also inserted in the file scope, but
    // to prevent this duplication in the XML output, we filter those here.
    if (d->definitionType()!=Definition::TypeFile || md->getNamespaceDef()==0)
    {
      generateSqlite3ForMember(md,d);
    }
  }
}

static void associateAllMembers(const ClassDef *cd)
{
  if (cd->memberNameInfoSDict())
  {
    struct Refid scope_refid = insertRefid(cd->getOutputFileBase());
    MemberNameInfoSDict::Iterator mnii(*cd->memberNameInfoSDict());
    MemberNameInfo *mni;
    for (mnii.toFirst();(mni=mnii.current());++mnii)
    {
      MemberNameInfoIterator mii(*mni);
      MemberInfo *mi;
      for (mii.toFirst();(mi=mii.current());++mii)
      {
        const MemberDef *md=mi->memberDef;
        if (md->name().at(0)!='@') // skip anonymous members
        {
          Protection prot = mi->prot;
          Specifier virt=md->virtualness();
          /* start preparing to insert */
          QCString qmember_refid = md->getOutputFileBase() + "_1" +
            md->anchor();


          struct Refid member_refid = insertRefid(qmember_refid);

          bindIntParameter(member_insert, ":scope_rowid", scope_refid.rowid);
          bindIntParameter(member_insert, ":memberdef_rowid", member_refid.rowid);

          bindIntParameter(member_insert, ":prot", md->protection());
          bindIntParameter(member_insert, ":virt", md->virtualness());

          if (!mi->ambiguityResolutionScope.isEmpty())
          {
            bindTextParameter(member_insert, ":ambiguityscope", mi->ambiguityResolutionScope);
          }
          step(member_insert);
        }
      }
    }
  }
}

// NOTE: using "x" as a marker for items that sqlite3 is missing and XML *claims* to be including. Treat the XML with some skepticism.
static void generateSqlite3ForClass(const ClassDef *cd)
{
  // + brief description
  // + detailed description
  // + template argument list(s)
  // + include file
  // + member groups
  // x inheritance DOT diagram
  // + list of direct super classes
  // + list of direct sub classes
  // + list of inner classes
  // x collaboration DOT diagram
  // + list of all members
  // x user defined member sections
  // x standard member sections
  // x detailed member documentation
  // - examples using the class

  if (cd->isReference())        return; // skip external references.
  if (cd->isHidden())           return; // skip hidden classes.
  if (cd->name().find('@')!=-1) return; // skip anonymous compounds.
  if (cd->templateMaster()!=0)  return; // skip generated template instances.

  msg("Generating Sqlite3 output for class %s\n",cd->name().data());

  struct Refid refid = insertRefid(cd->getOutputFileBase());
  if(!refid.created && compounddefExists(refid)){return;}// in theory we can omit a class that already has a refid--unless there are conditions under which we may encounter the class refid before parsing the class? Might want to create a test or assertion for this?

  bindIntParameter(compounddef_insert,":rowid", refid.rowid);

  bindTextParameter(compounddef_insert,":name",cd->name());
  bindTextParameter(compounddef_insert,":title",cd->title(), FALSE);
  bindTextParameter(compounddef_insert,":kind",cd->compoundTypeString(),FALSE);
  bindIntParameter(compounddef_insert,":prot",cd->protection());

  int file_id = insertFile(cd->getDefFileName());
  bindIntParameter(compounddef_insert,":file_id",file_id);
  bindIntParameter(compounddef_insert,":line",cd->getDefLine());
  bindIntParameter(compounddef_insert,":column",cd->getDefColumn());

  getSQLDesc(compounddef_insert,":briefdescription",cd->briefDescription(),cd);
  getSQLDesc(compounddef_insert,":detaileddescription",cd->documentation(),cd);

  step(compounddef_insert);

  // + list of direct super classes
  if (cd->baseClasses())
  {
    BaseClassListIterator bcli(*cd->baseClasses());
    const BaseClassDef *bcd;
    for (bcli.toFirst();(bcd=bcli.current());++bcli)
    {
      struct Refid base_refid = insertRefid(bcd->classDef->getOutputFileBase());
      struct Refid derived_refid = insertRefid(cd->getOutputFileBase());
      bindIntParameter(compoundref_insert,":base_rowid", base_refid.rowid);
      bindIntParameter(compoundref_insert,":derived_rowid", derived_refid.rowid);
      bindIntParameter(compoundref_insert,":prot",bcd->prot);
      bindIntParameter(compoundref_insert,":virt",bcd->virt);
      step(compoundref_insert);
    }
  }

  // + list of direct sub classes
  if (cd->subClasses())
  {
    BaseClassListIterator bcli(*cd->subClasses());
    const BaseClassDef *bcd;
    for (bcli.toFirst();(bcd=bcli.current());++bcli)
    {
      struct Refid derived_refid = insertRefid(bcd->classDef->getOutputFileBase());
      struct Refid base_refid = insertRefid(cd->getOutputFileBase());
      bindIntParameter(compoundref_insert,":base_rowid", base_refid.rowid);
      bindIntParameter(compoundref_insert,":derived_rowid", derived_refid.rowid);
      bindIntParameter(compoundref_insert,":prot",bcd->prot);
      bindIntParameter(compoundref_insert,":virt",bcd->virt);
      step(compoundref_insert);
    }
  }

  // + include file
  IncludeInfo *ii=cd->includeInfo();
  if (ii)
  {
    QCString nm = ii->includeName;
    if (nm.isEmpty() && ii->fileDef) nm = ii->fileDef->docName();
    if (!nm.isEmpty())
    {
      int dst_id=insertFile(ii->fileDef->absFilePath());
      if (dst_id!=-1) {
        bindIntParameter(incl_select,":local",ii->local);
        bindIntParameter(incl_select,":src_id",file_id);
        bindIntParameter(incl_select,":dst_id",dst_id);
        int count=step(incl_select,TRUE,TRUE);
        if (count==0)
        {
          bindIntParameter(incl_insert,":local",ii->local);
          bindIntParameter(incl_insert,":src_id",file_id);
          bindIntParameter(incl_insert,":dst_id",dst_id);
          step(incl_insert);
        }
      }
    }
  }
  // + list of inner classes
  writeInnerClasses(cd->getClassSDict(),refid);

  // + template argument list(s)
  writeTemplateList(cd);

  // + member groups
  if (cd->getMemberGroupSDict())
  {
    MemberGroupSDict::Iterator mgli(*cd->getMemberGroupSDict());
    MemberGroup *mg;
    for (;(mg=mgli.current());++mgli)
    {
      generateSqlite3Section(cd,mg->members(),"user-defined",mg->header(),
          mg->documentation());
    }
  }

  // + list of all members
  // NOTE: this is just a list of *local* members, there's a completely separate process for identifying the sort of meta list xmlgen uses to scoop up inherited members
  QListIterator<MemberList> mli(cd->getMemberLists());
  MemberList *ml;
  for (mli.toFirst();(ml=mli.current());++mli)
  {
    if ((ml->listType()&MemberListType_detailedLists)==0)
    {
      generateSqlite3Section(cd,ml,"user-defined");//g_xmlSectionMapper.find(ml->listType()));
    }
  }
  associateAllMembers(cd);
}

static void generateSqlite3ForNamespace(const NamespaceDef *nd)
{
  // + contained class definitions
  // + contained namespace definitions
  // + member groups
  // + normal members
  // + brief desc
  // + detailed desc
  // + location (file_id, line, column)
  // - files containing (parts of) the namespace definition

  if (nd->isReference() || nd->isHidden()) return; // skip external references
  struct Refid refid = insertRefid(nd->getOutputFileBase());
  if(!refid.created && compounddefExists(refid)){return;}
  bindIntParameter(compounddef_insert,":rowid", refid.rowid);

  bindTextParameter(compounddef_insert,":name",nd->name());
  bindTextParameter(compounddef_insert,":title",nd->title(), FALSE);
  bindTextParameter(compounddef_insert,":kind","namespace",FALSE);

  int file_id = insertFile(nd->getDefFileName());
  bindIntParameter(compounddef_insert,":file_id",file_id);
  bindIntParameter(compounddef_insert,":line",nd->getDefLine());
  bindIntParameter(compounddef_insert,":column",nd->getDefColumn());

  getSQLDesc(compounddef_insert,":briefdescription",nd->briefDescription(),nd);
  getSQLDesc(compounddef_insert,":detaileddescription",nd->documentation(),nd);

  step(compounddef_insert);

  // + contained class definitions
  writeInnerClasses(nd->getClassSDict(),refid);

  // + contained namespace definitions
  writeInnerNamespaces(nd->getNamespaceSDict(),refid);

  // + member groups
  if (nd->getMemberGroupSDict())
  {
    MemberGroupSDict::Iterator mgli(*nd->getMemberGroupSDict());
    MemberGroup *mg;
    for (;(mg=mgli.current());++mgli)
    {
      generateSqlite3Section(nd,mg->members(),"user-defined",mg->header(),
          mg->documentation());
    }
  }

  // + normal members
  QListIterator<MemberList> mli(nd->getMemberLists());
  MemberList *ml;
  for (mli.toFirst();(ml=mli.current());++mli)
  {
    if ((ml->listType()&MemberListType_declarationLists)!=0)
    {
      generateSqlite3Section(nd,ml,"user-defined");//g_xmlSectionMapper.find(ml->listType()));
    }
  }
}

static void generateSqlite3ForFile(const FileDef *fd)
{
  // + includes files
  // + includedby files
  // x include graph
  // x included by graph
  // + contained class definitions
  // + contained namespace definitions
  // + member groups
  // + normal members
  // + brief desc
  // + detailed desc
  // x source code
  // + location (file_id, line, column)
  // - number of lines

  if (fd->isReference()) return; // skip external references

  struct Refid refid = insertRefid(fd->getOutputFileBase());
  if(!refid.created && compounddefExists(refid)){return;}
  bindIntParameter(compounddef_insert,":rowid", refid.rowid);

  bindTextParameter(compounddef_insert,":name",fd->name(),FALSE);
  bindTextParameter(compounddef_insert,":title",fd->title(),FALSE);
  bindTextParameter(compounddef_insert,":kind","file",FALSE);

  int file_id = insertFile(fd->getDefFileName());
  bindIntParameter(compounddef_insert,":file_id",file_id);
  bindIntParameter(compounddef_insert,":line",fd->getDefLine());
  bindIntParameter(compounddef_insert,":column",fd->getDefColumn());

  getSQLDesc(compounddef_insert,":briefdescription",fd->briefDescription(),fd);
  getSQLDesc(compounddef_insert,":detaileddescription",fd->documentation(),fd);

  step(compounddef_insert);

  // + includes files
  IncludeInfo *ii;
  if (fd->includeFileList())
  {
    QListIterator<IncludeInfo> ili(*fd->includeFileList());
    for (ili.toFirst();(ii=ili.current());++ili)
    {
      int src_id=insertFile(fd->absFilePath());
      int dst_id=insertFile(ii->fileDef->absFilePath());
      bindIntParameter(incl_select,":local",ii->local);
      bindIntParameter(incl_select,":src_id",src_id);
      bindIntParameter(incl_select,":dst_id",dst_id);
      if (step(incl_select,TRUE,TRUE)==0) {
        bindIntParameter(incl_insert,":local",ii->local);
        bindIntParameter(incl_insert,":src_id",src_id);
        bindIntParameter(incl_insert,":dst_id",dst_id);
        step(incl_insert);
      }
    }
  }

  // + includedby files
  if (fd->includedByFileList())
  {
    QListIterator<IncludeInfo> ili(*fd->includedByFileList());
    for (ili.toFirst();(ii=ili.current());++ili)
    {
      int src_id=insertFile(ii->fileDef->absFilePath());
      int dst_id=insertFile(fd->absFilePath());
      bindIntParameter(incl_select,":local",ii->local);
      bindIntParameter(incl_select,":src_id",src_id);
      bindIntParameter(incl_select,":dst_id",dst_id);
      if (step(incl_select,TRUE,TRUE)==0) {
        bindIntParameter(incl_insert,":local",ii->local);
        bindIntParameter(incl_insert,":src_id",src_id);
        bindIntParameter(incl_insert,":dst_id",dst_id);
        step(incl_insert);
      }
    }
  }

  // + contained class definitions
  if (fd->getClassSDict())
  {
    writeInnerClasses(fd->getClassSDict(),refid);
  }

  // + contained namespace definitions
  if (fd->getNamespaceSDict())
  {
    writeInnerNamespaces(fd->getNamespaceSDict(),refid);
  }

  // + member groups
  if (fd->getMemberGroupSDict())
  {
    MemberGroupSDict::Iterator mgli(*fd->getMemberGroupSDict());
    MemberGroup *mg;
    for (;(mg=mgli.current());++mgli)
    {
      generateSqlite3Section(fd,mg->members(),"user-defined",mg->header(),
          mg->documentation());
    }
  }

  // + normal members
  QListIterator<MemberList> mli(fd->getMemberLists());
  MemberList *ml;
  for (mli.toFirst();(ml=mli.current());++mli)
  {
    if ((ml->listType()&MemberListType_declarationLists)!=0)
    {
      generateSqlite3Section(fd,ml,"user-defined");//g_xmlSectionMapper.find(ml->listType()));
    }
  }
}

static void generateSqlite3ForGroup(const GroupDef *gd)
{
  // + members
  // + member groups
  // + files
  // + classes
  // + namespaces
  // - packages
  // + pages
  // + child groups
  // - examples
  // + brief description
  // + detailed description

  if (gd->isReference())        return; // skip external references.

  struct Refid refid = insertRefid(gd->getOutputFileBase());
  if(!refid.created && compounddefExists(refid)){return;}
  bindIntParameter(compounddef_insert,":rowid", refid.rowid);

  bindTextParameter(compounddef_insert,":name",gd->name());
  bindTextParameter(compounddef_insert,":title",gd->groupTitle(), FALSE);
  bindTextParameter(compounddef_insert,":kind","group",FALSE);

  int file_id = insertFile(gd->getDefFileName());
  bindIntParameter(compounddef_insert,":file_id",file_id);
  bindIntParameter(compounddef_insert,":line",gd->getDefLine());
  bindIntParameter(compounddef_insert,":column",gd->getDefColumn());

  getSQLDesc(compounddef_insert,":briefdescription",gd->briefDescription(),gd);
  getSQLDesc(compounddef_insert,":detaileddescription",gd->documentation(),gd);

  step(compounddef_insert);

  // + files
  writeInnerFiles(gd->getFiles(),refid);

  // + classes
  writeInnerClasses(gd->getClasses(),refid);

  // + namespaces
  writeInnerNamespaces(gd->getNamespaces(),refid);

  // + pages
  writeInnerPages(gd->getPages(),refid);

  // + groups
  writeInnerGroups(gd->getSubGroups(),refid);

  // + member groups
  if (gd->getMemberGroupSDict())
  {
    MemberGroupSDict::Iterator mgli(*gd->getMemberGroupSDict());
    MemberGroup *mg;
    for (;(mg=mgli.current());++mgli)
    {
      generateSqlite3Section(gd,mg->members(),"user-defined",mg->header(),
          mg->documentation());
    }
  }

  // + members
  QListIterator<MemberList> mli(gd->getMemberLists());
  MemberList *ml;
  for (mli.toFirst();(ml=mli.current());++mli)
  {
    if ((ml->listType()&MemberListType_declarationLists)!=0)
    {
      generateSqlite3Section(gd,ml,"user-defined");
    }
  }
}

static void generateSqlite3ForDir(const DirDef *dd)
{
  // TODO: should there be more here? the matching function in xmlgen doesn't have a list like this, so I'm just building it from what xmlgen DOES include (thus missing anything that could be included but isn't in xmlgen)
  // + dirs
  // + files
  // + briefdescription
  // + detaileddescription
  // + location (cleanup: I'm using file_id, line, column as usual, but XML just uses file; line/col may break or do nothing)
  if (dd->isReference()) return; // skip external references

  struct Refid refid = insertRefid(dd->getOutputFileBase());
  if(!refid.created && compounddefExists(refid)){return;}
  bindIntParameter(compounddef_insert,":rowid", refid.rowid);

  bindTextParameter(compounddef_insert,":name",dd->displayName());
  bindTextParameter(compounddef_insert,":kind","dir",FALSE);

  int file_id = insertFile(dd->getDefFileName());
  bindIntParameter(compounddef_insert,":file_id",file_id);
  bindIntParameter(compounddef_insert,":line",dd->getDefLine());
  bindIntParameter(compounddef_insert,":column",dd->getDefColumn());

  getSQLDesc(compounddef_insert,":briefdescription",dd->briefDescription(),dd);
  getSQLDesc(compounddef_insert,":detaileddescription",dd->documentation(),dd);

  step(compounddef_insert);

  // + files
  writeInnerDirs(&dd->subDirs(),refid);

  // + files
  writeInnerFiles(dd->getFiles(),refid);
}

// pages are just another kind of compound
// kinds of compound: class, struct, union, interface, protocol, category, exception, service, singleton, module, type, file, namespace, group, page, example, dir
static void generateSqlite3ForPage(const PageDef *pd,bool isExample)
{
  // + name
  // + title
  // + brief description
  // + documentation (detailed description)
  // + inbody documentation
  // + sub pages
  if (pd->isReference())        return; // skip external references.

  QCString qrefid = pd->getOutputFileBase();
  if (pd->getGroupDef())
  {
    qrefid+=(QCString)"_"+pd->name();
  }
  if (qrefid=="index") qrefid="indexpage"; // to prevent overwriting the generated index page.

  struct Refid refid = insertRefid(qrefid);
  if(!refid.created && compounddefExists(refid)){return;}// in theory we can omit a class that already has a refid--unless there are conditions under which we may encounter the class refid before parsing the class? Might want to create a test or assertion for this?

  bindIntParameter(compounddef_insert,":rowid",refid.rowid);
  // + name
  bindTextParameter(compounddef_insert,":name",pd->name());

  QCString title;
  if (pd==Doxygen::mainPage) // main page is special
  {
    if (!pd->title().isEmpty() && pd->title().lower()!="notitle")
    {
      title = filterTitle(convertCharEntitiesToUTF8(Doxygen::mainPage->title()));
    }
    else
    {
      title = Config_getString(PROJECT_NAME);
    }
  }
  else
  {
    SectionInfo *si = Doxygen::sectionDict->find(pd->name());
    if (si)
    {
      title = si->title;
    }

    if(!title){title = pd->title();}
  }

  // + title
  bindTextParameter(compounddef_insert,":title",title,FALSE);

  bindTextParameter(compounddef_insert,":kind", isExample ? "example" : "page");

  int file_id = insertFile(pd->getDefFileName());

  bindIntParameter(compounddef_insert,":file_id",file_id);
  bindIntParameter(compounddef_insert,":line",pd->getDefLine());
  bindIntParameter(compounddef_insert,":column",pd->getDefColumn());

  // + brief description
  getSQLDesc(compounddef_insert,":briefdescription",pd->briefDescription(),pd);
  // + documentation (detailed description)
  getSQLDesc(compounddef_insert,":detaileddescription",pd->documentation(),pd);

  step(compounddef_insert);
  // + sub pages
  writeInnerPages(pd->getSubPages(),refid);

  // TODO: xml gen handles this slightly differently if isExample; do we need to differ here?
  //bindTextParameter(compounddef_insert,":refid",bcd->classDef->getOutputFileBase(),FALSE);
  //isExample
}


static sqlite3* openDbConnection()
{

  QCString outputDirectory = Config_getString(OUTPUT_DIRECTORY);
  QDir sqlite3Dir(outputDirectory);
  sqlite3 *db;
  int rc;

  rc = sqlite3_initialize();
  if (rc != SQLITE_OK)
  {
    msg("sqlite3_initialize failed\n");
    return NULL;
  }
  rc = sqlite3_open_v2(outputDirectory+"/doxygen_sqlite3.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
  if (rc != SQLITE_OK)
  {
    sqlite3_close(db);
    msg("database open failed: %s\n", "doxygen_sqlite3.db");
    return NULL;
  }
  return db;
}
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
void generateSqlite3()
{
  // + classes
  // + namespaces
  // + files
  // + groups
  // + related pages
  // + examples
  // + main page
  sqlite3 *db;

  db = openDbConnection();
  if (db==NULL)
  {
    return;
  }
  // debug: enable below to see all executed statements
  // sqlite3_trace(db, &sqlLog, NULL);
  beginTransaction(db);
  pragmaTuning(db);

  if (-1==initializeTables(db))
    return;

  if ( -1 == prepareStatements(db) )
  {
    err("sqlite generator: prepareStatements failed!");
    return;
  }

  recordMetadata();

  // + classes
  ClassSDict::Iterator cli(*Doxygen::classSDict);
  const ClassDef *cd;
  for (cli.toFirst();(cd=cli.current());++cli)
  {
    msg("Generating Sqlite3 output for class %s\n",cd->name().data());
    generateSqlite3ForClass(cd);
  }

  // + namespaces
  NamespaceSDict::Iterator nli(*Doxygen::namespaceSDict);
  const NamespaceDef *nd;
  for (nli.toFirst();(nd=nli.current());++nli)
  {
    msg("Generating Sqlite3 output for namespace %s\n",nd->name().data());
    generateSqlite3ForNamespace(nd);
  }

  // + files
  FileNameListIterator fnli(*Doxygen::inputNameList);
  FileName *fn;
  for (;(fn=fnli.current());++fnli)
  {
    FileNameIterator fni(*fn);
    const FileDef *fd;
    for (;(fd=fni.current());++fni)
    {
      msg("Generating Sqlite3 output for file %s\n",fd->name().data());
      generateSqlite3ForFile(fd);
    }
  }

  // + groups
  GroupSDict::Iterator gli(*Doxygen::groupSDict);
  const GroupDef *gd;
  for (;(gd=gli.current());++gli)
  {
    msg("Generating Sqlite3 output for group %s\n",gd->name().data());
    generateSqlite3ForGroup(gd);
  }

  // + page
  {
    PageSDict::Iterator pdi(*Doxygen::pageSDict);
    const PageDef *pd=0;
    for (pdi.toFirst();(pd=pdi.current());++pdi)
    {
      msg("Generating Sqlite3 output for page %s\n",pd->name().data());
      generateSqlite3ForPage(pd,FALSE);
    }
  }

  // + dirs
  {
    const DirDef *dir;
    DirSDict::Iterator sdi(*Doxygen::directories);
    for (sdi.toFirst();(dir=sdi.current());++sdi)
    {
      msg("Generating Sqlite3 output for dir %s\n",dir->name().data());
      generateSqlite3ForDir(dir);
    }
  }

  // + examples
  {
    PageSDict::Iterator pdi(*Doxygen::exampleSDict);
    const PageDef *pd=0;
    for (pdi.toFirst();(pd=pdi.current());++pdi)
    {
      msg("Generating Sqlite3 output for example %s\n",pd->name().data());
      generateSqlite3ForPage(pd,TRUE);
    }
  }

  // + main page
  if (Doxygen::mainPage)
  {
    msg("Generating Sqlite3 output for the main page\n");
    generateSqlite3ForPage(Doxygen::mainPage,FALSE);
  }

  if (-1==initializeViews(db))
    return; // TODO: copied from initializeSchema; not certain if there's a more appropriate action to take on a failure here?

  endTransaction(db);
}

#else // USE_SQLITE3
void generateSqlite3()
{
  err("sqlite3 support has not been compiled in!");
}
#endif
// vim: noai:ts=2:sw=2:ss=2:expandtab
