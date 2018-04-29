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
#include "config.h"
#include "util.h"
#include "docparser.h"
#include "language.h"

#include "dot.h"
#include "arguments.h"
#include "classlist.h"
#include "filedef.h"
#include "namespacedef.h"
#include "filename.h"
#include "groupdef.h"
#include "pagedef.h"
#include "dirdef.h"

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
  { "includes",
    "CREATE TABLE IF NOT EXISTS includes (\n"
      "\t-- #include relations.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tlocal        INTEGER NOT NULL,\n"
      "\tsrc_id       INTEGER NOT NULL,  -- File id of the includer.\n"
      "\tdst_id       INTEGER NOT NULL,   -- File id of the includee.\n"
      "\tUNIQUE(local, src_id, dst_id) ON CONFLICT IGNORE\n"
      ");"
  },
  { "innerclass",
    "CREATE TABLE IF NOT EXISTS innerclass (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tprot         INTEGER NOT NULL,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "innerpage",
    "CREATE TABLE IF NOT EXISTS innerpage (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "innernamespace",
    "CREATE TABLE IF NOT EXISTS innernamespace (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "innergroup",
    "CREATE TABLE IF NOT EXISTS innergroup (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "innerfile",
    "CREATE TABLE IF NOT EXISTS innerfile (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "innerdir",
    "CREATE TABLE IF NOT EXISTS innerdir (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tinner_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\touter_refid  INTEGER NOT NULL REFERENCES compounddef,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "files",
    "CREATE TABLE IF NOT EXISTS files (\n"
      "\t-- Names of source files and includes.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tname         TEXT NOT NULL\n"
      ");"
  },
  { "refids",
    "CREATE TABLE IF NOT EXISTS refids (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\trefid        TEXT NOT NULL UNIQUE\n"
      ");"
  },
  /* xrefs table combines the xml <referencedby> and <references> nodes */
  { "xrefs",
    "CREATE TABLE IF NOT EXISTS xrefs (\n"
      "\t-- Cross reference relation.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tsrc_refid    INTEGER NOT NULL REFERENCES refids, -- referrer id.\n"
      "\tdst_refid    INTEGER NOT NULL REFERENCES refids, -- referee id.\n"
      "\tUNIQUE(src_refid, dst_refid) ON CONFLICT IGNORE\n"
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
      "\tinline               INTEGER DEFAULT 0, -- 0:no 1:yes\n"
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
      /// @todo make a `kind' table
      "\tkind                 INTEGER DEFAULT 0, -- 0:define 1:function 2:variable 3:typedef 4:enum 5:enumvalue 6:signal 7:slot 8:friend 9:DCOP 10:property 11:event\n"
      "\tbodystart            INTEGER DEFAULT 0, -- starting line of definition\n"
      "\tbodyend              INTEGER DEFAULT 0, -- ending line of definition\n"
      "\tid_bodyfile          INTEGER DEFAULT 0, -- file of definition\n"
      "\tid_file              INTEGER NOT NULL,  -- file where this identifier is located\n"
      "\tline                 INTEGER NOT NULL,  -- line where this identifier is located\n"
      "\tcolumn               INTEGER NOT NULL,  -- column where this identifier is located\n"
      /// @todo make a `detaileddescription' table
      "\tdetaileddescription  TEXT,\n"
      "\tbriefdescription     TEXT,\n"
      "\tinbodydescription    TEXT\n"
      "\tFOREIGN KEY (rowid) REFERENCES refids (rowid)\n"
      ");"
  },
  { "compounddef",
    "CREATE TABLE IF NOT EXISTS compounddef (\n"
      "\t-- class/struct definitions.\n"
      "\tname         TEXT NOT NULL,\n"
      "\trowid                INTEGER PRIMARY KEY NOT NULL,\n"
      "\tkind         TEXT NOT NULL,\n"
      "\tFOREIGN KEY (rowid) REFERENCES refids (rowid)\n"
      "\tprot         INTEGER NOT NULL,\n"
      "\tid_file      INTEGER NOT NULL,\n"
      "\tline         INTEGER NOT NULL,\n"
      "\tcolumn       INTEGER NOT NULL\n"
      ");"
  },
  { "basecompoundref",
    "CREATE TABLE IF NOT EXISTS basecompoundref (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tbase         TEXT NOT NULL,\n"
      "\tderived      TEXT NOT NULL,\n"
      "\trefid        INTEGER NOT NULL,\n"
      "\tprot         INTEGER NOT NULL,\n"
      "\tvirt         INTEGER NOT NULL\n"
      ");"
  },
  { "derivedcompoundref",
    "CREATE TABLE IF NOT EXISTS derivedcompoundref (\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tbase         TEXT NOT NULL,\n"
      "\tderived      TEXT NOT NULL,\n"
      "\trefid        INTEGER NOT NULL,\n"
      "\tprot         INTEGER NOT NULL,\n"
      "\tvirt         INTEGER NOT NULL\n"
      ");"
  },
  { "params",
    "CREATE TABLE IF NOT EXISTS params (\n"
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
    "CREATE UNIQUE INDEX idx_params ON params\n"
      "\t(type, defname);"
  },
  { "memberdef_params",
    "CREATE TABLE IF NOT EXISTS memberdef_params (\n"
      "\t-- Junction table for memberdef parameters.\n"
      "\trowid        INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,\n"
      "\tid_memberdef INTEGER NOT NULL,\n"
      "\tid_param     INTEGER NOT NULL\n"
      ");"
  },
};
  const char * view_schema[][2] = {
  /* VIEWS */
  {
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
   sqlite3_clear_bindings(), using an un-prepared statement may
   be the cause. */
SqlStmt incl_insert = { "INSERT INTO includes "
  "( local, src_id, dst_id ) "
    "VALUES "
    "(:local,:src_id,:dst_id )"
    ,NULL
};
SqlStmt incl_select = { "SELECT COUNT(*) FROM includes WHERE "
  "local=:local AND src_id=:src_id AND dst_id=:dst_id"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt innerclass_insert={"INSERT INTO innerclass "
    "( inner_refid, outer_refid, prot, name )"
    "VALUES "
    "(:inner_refid,:outer_refid,:prot,:name )"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt innerpage_insert={"INSERT INTO innerpage "
    "( inner_refid, outer_refid, name )"
    "VALUES "
    "(:inner_refid,:outer_refid,:name )"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt innernamespace_insert={"INSERT INTO innernamespace "
    "( inner_refid, outer_refid, name)"
    "VALUES "
    "(:inner_refid,:outer_refid,:name)",
    NULL
};
//////////////////////////////////////////////////////
SqlStmt innergroup_insert={"INSERT INTO innergroup "
    "( inner_refid, outer_refid, name)"
    "VALUES "
    "(:inner_refid,:outer_refid,:name)",
    NULL
};
//////////////////////////////////////////////////////
SqlStmt innerdir_insert={"INSERT INTO innerdir "
    "( inner_refid, outer_refid, name)"
    "VALUES "
    "(:inner_refid,:outer_refid,:name)",
    NULL
};
//////////////////////////////////////////////////////
SqlStmt innerfile_insert={"INSERT INTO innerfile "
    "( inner_refid, outer_refid, name)"
    "VALUES "
    "(:inner_refid,:outer_refid,:name)",
    NULL
};
//////////////////////////////////////////////////////
SqlStmt files_select = {"SELECT rowid FROM files WHERE name=:name"
  ,NULL
};
SqlStmt files_insert = {"INSERT INTO files "
  "( name )"
    "VALUES "
    "(:name )"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt refids_select =  {"SELECT rowid FROM refids WHERE "
  "refid=:refid"
    ,NULL
};
SqlStmt refids_insert = {"INSERT INTO refids "
  "( refid )"
    "VALUES "
    "(:refid )"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt xrefs_insert= {"INSERT INTO xrefs "
  "( src_refid, dst_refid )"
    "VALUES "
    "(:src_refid,:dst_refid )"
    ,NULL
};//////////////////////////////////////////////////////
};
//////////////////////////////////////////////////////
SqlStmt memberdef_exists={"SELECT EXISTS (SELECT * FROM memberdef WHERE rowid = :rowid)"
    ,NULL
};

SqlStmt memberdef_incomplete={"SELECT EXISTS (SELECT * FROM memberdef WHERE rowid = :rowid AND inline != 2 AND inline != :new_inline)"
    ,NULL
};

SqlStmt memberdef_insert={"INSERT INTO memberdef "
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
      "id_bodyfile,"
      "id_file,"
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
      ":id_bodyfile,"
      ":id_file,"
      ":line,"
      ":column,"
      ":detaileddescription,"
      ":briefdescription,"
      ":inbodydescription"
    ")"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt compounddef_insert={"INSERT INTO compounddef "
    "( rowid, name, kind, prot, id_file, line, column ) "
    "VALUES "
    "(:rowid, :name,:kind,:prot,:id_file,:line,:column )"
    ,NULL
};
SqlStmt compounddef_exists={"SELECT EXISTS (SELECT * FROM compounddef WHERE rowid = :rowid)"
    ,NULL
};
    "VALUES "
    "(:base,:derived,:refid,:prot,:virt )"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt derivedcompoundref_insert={"INSERT INTO  derivedcompoundref "
    "( refid, prot, virt, base, derived ) "
    "VALUES "
    "(:refid,:prot,:virt,:base,:derived )"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt params_select = { "SELECT rowid FROM  params WHERE "
    "(attributes IS NULL OR attributes=:attributes) AND "
    "(type IS NULL OR type=:type) AND "
    "(declname IS NULL OR declname=:declname) AND "
    "(defname IS NULL OR defname=:defname) AND "
    "(array IS NULL OR array=:array) AND "
    "(defval IS NULL OR defval=:defval) AND "
    "(briefdescription IS NULL OR briefdescription=:briefdescription)"
    ,NULL
};
SqlStmt params_insert = { "INSERT INTO  params "
  "( attributes, type, declname, defname, array, defval, briefdescription ) "
    "VALUES "
    "(:attributes,:type,:declname,:defname,:array,:defval,:briefdescription)"
    ,NULL
};
//////////////////////////////////////////////////////
SqlStmt memberdef_params_insert={ "INSERT INTO  memberdef_params "
    "( id_memberdef, id_param)"
    "VALUES "
    "(:id_memberdef,:id_param)"
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

  bindTextParameter(files_select,":name",name);
  rowid=step(files_select,TRUE,TRUE);
  if (rowid==0)
  {
    bindTextParameter(files_insert,":name",name);
    rowid=step(files_insert,TRUE);
  }
  return rowid;
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

  bindTextParameter(refids_select,":refid",refid);
  ret.rowid=step(refids_select,TRUE,TRUE);
  if (ret.rowid==0)
  {
    bindTextParameter(refids_insert,":refid",refid);
    ret.rowid=step(refids_insert,TRUE);
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

static bool insertMemberReference(struct Refid src_refid, struct Refid dst_refid)
{
  if (src_refid.rowid==-1||dst_refid.rowid==-1)
    return false;

  if (
     !bindIntParameter(xrefs_insert,":src_refid",src_refid.rowid) ||
     !bindIntParameter(xrefs_insert,":dst_refid",dst_refid.rowid)
     )
  {
    return false;
  }

  step(xrefs_insert);
  return true;
}

static void insertMemberReference(const MemberDef *src, const MemberDef *dst)
{
  QCString qrefid_dst = dst->getOutputFileBase() + "_1" + dst->anchor();
  QCString qrefid_src = src->getOutputFileBase() + "_1" + src->anchor();
  if (dst->getStartBodyLine()!=-1 && dst->getBodyDef())
  {
    int refid_src = insertRefid(qrefid_src.data());
    int refid_dst = insertRefid(qrefid_dst.data());
    int id_file = insertFile("no-file"); // TODO: replace no-file with proper file
    insertMemberReference(refid_src,refid_dst,id_file,dst->getStartBodyLine(),-1);
  }
}

static void insertMemberFunctionParams(int id_memberdef, const MemberDef *md, const Definition *def)
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
        bindTextParameter(params_select,":attributes",a->attrib.data());
        bindTextParameter(params_insert,":attributes",a->attrib.data());
      }
      if (!a->type.isEmpty())
      {
        StringList l;
        linkifyText(TextGeneratorSqlite3Impl(l),def,md->getBodyDef(),md,a->type);

        StringListIterator li(l);
        QCString *s;
        while ((s=li.current()))
        {
          QCString qrefid_src = md->getOutputFileBase() + "_1" + md->anchor();
          int refid_src = insertRefid(qrefid_src.data());
          int refid_dst = insertRefid(s->data());
          int id_file = insertFile(stripFromPath(def->getDefFileName()));
          insertMemberReference(refid_src,refid_dst,id_file,md->getDefLine(),-1);
          ++li;
        }
        bindTextParameter(params_select,":type",a->type.data());
        bindTextParameter(params_insert,":type",a->type.data());
      }
      if (!a->name.isEmpty())
      {
        bindTextParameter(params_select,":declname",a->name.data());
        bindTextParameter(params_insert,":declname",a->name.data());
      }
      if (defArg && !defArg->name.isEmpty() && defArg->name!=a->name)
      {
        bindTextParameter(params_select,":defname",defArg->name.data());
        bindTextParameter(params_insert,":defname",defArg->name.data());
      }
      if (!a->array.isEmpty())
      {
        bindTextParameter(params_select,":array",a->array.data());
        bindTextParameter(params_insert,":array",a->array.data());
      }
      if (!a->defval.isEmpty())
      {
        StringList l;
        linkifyText(TextGeneratorSqlite3Impl(l),def,md->getBodyDef(),md,a->defval);
        bindTextParameter(params_select,":defval",a->defval.data());
        bindTextParameter(params_insert,":defval",a->defval.data());
      }
      if (defArg) ++defAli;

      int id_param=step(params_select,TRUE,TRUE);
      if (id_param==0) {
        id_param=step(params_insert,TRUE);
      }
      if (id_param==-1) {
          msg("error INSERT params failed\n");
          continue;
      }

      bindIntParameter(memberdef_params_insert,":id_memberdef",id_memberdef);
      bindIntParameter(memberdef_params_insert,":id_param",id_param);
      step(memberdef_params_insert);
    }
  }
}

static void insertMemberDefineParams(int id_memberdef,const MemberDef *md, const Definition *def)
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
        bindTextParameter(params_insert,":defname",a->type.data());
        int id_param=step(params_insert,TRUE);
        if (id_param==-1) {
          msg("error INSERT param(%s) failed\n", a->type.data());
          continue;
        }

        bindIntParameter(memberdef_params_insert,":id_memberdef",id_memberdef);
        bindIntParameter(memberdef_params_insert,":id_param",id_param);
        step(memberdef_params_insert);
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
  -1==prepareStatement(db, memberdef_exists) ||
  -1==prepareStatement(db, memberdef_incomplete) ||
  -1==prepareStatement(db, memberdef_insert) ||
  -1==prepareStatement(db, files_insert) ||
  -1==prepareStatement(db, files_select) ||
  -1==prepareStatement(db, refids_insert) ||
  -1==prepareStatement(db, refids_select) ||
  -1==prepareStatement(db, incl_insert)||
  -1==prepareStatement(db, incl_select)||
  -1==prepareStatement(db, params_insert) ||
  -1==prepareStatement(db, params_select) ||
  -1==prepareStatement(db, xrefs_insert) ||
  -1==prepareStatement(db, innerclass_insert) ||
  -1==prepareStatement(db, innerpage_insert) ||
  -1==prepareStatement(db, innergroup_insert) ||
  -1==prepareStatement(db, innerfile_insert) ||
  -1==prepareStatement(db, innerdir_insert) ||
  -1==prepareStatement(db, compounddef_exists) ||
  -1==prepareStatement(db, compounddef_insert) ||
  -1==prepareStatement(db, basecompoundref_insert) ||
  -1==prepareStatement(db, derivedcompoundref_insert) ||
  -1==prepareStatement(db, memberdef_params_insert)||
  -1==prepareStatement(db, innernamespace_insert)
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

  msg("Initializing DB schema...\n");
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

  msg("Initializing DB schema...\n");
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

      bindIntParameter(innerclass_insert,":inner_refid", inner_refid.rowid);
      bindIntParameter(innerclass_insert,":outer_refid", outer_refid.rowid);
      bindIntParameter(innerclass_insert,":prot",cd->protection());
      bindTextParameter(innerclass_insert,":name",cd->name());
      step(innerclass_insert);
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

        bindIntParameter(innernamespace_insert,":inner_refid",inner_refid.rowid);
        bindIntParameter(innernamespace_insert,":outer_refid",outer_refid.rowid);
        step(innernamespace_insert);
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
        bindTextParameter(params_select,":type",a->type);
        bindTextParameter(params_insert,":type",a->type);
      }
      if (!a->name.isEmpty())
      {
        bindTextParameter(params_select,":declname",a->name);
        bindTextParameter(params_insert,":declname",a->name);
        bindTextParameter(params_select,":defname",a->name);
        bindTextParameter(params_insert,":defname",a->name);
      }
      if (!a->defval.isEmpty())
      {
        #warning linkifyText(TextGeneratorXMLImpl(t),scope,fileScope,0,a->defval);
        bindTextParameter(params_select,":defval",a->defval);
        bindTextParameter(params_insert,":defval",a->defval);
      }
      if (!step(params_select,TRUE,TRUE))
        step(params_insert);
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
////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
static void generateSqlite3ForMember(const MemberDef *md, const Definition *def)
{
  // + declaration/definition arg lists
  // + reimplements
  // NOTE: This erroneously claimed from the first commit that it covered reimplements, reimplementedBy & exceptions
  // + reimplementedBy
  // - reimplements
  // + exceptions
  // - reimplementedBy
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
  int refid = insertRefid(qrefid.data());

  bindIntParameter(memberdef_insert,":kind",md->memberType());
  bindIntParameter(memberdef_insert,":rowid", refid.rowid);
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

    //TODO:: This seems like a lot of work to find the accessor; I wonder if there's a shortcut?
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
    if (typeStr.data())
    {
      bindTextParameter(memberdef_insert,":type",typeStr.data(),FALSE);
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
    bindTextParameter(memberdef_insert,":initializer",md->initializer().data());

    StringList l;
    linkifyText(TextGeneratorSqlite3Impl(l),def,md->getBodyDef(),md,md->initializer());
    StringListIterator li(l);
    QCString *s;
    while ((s=li.current()))
    {
      if (md->getBodyDef())
      {
        DBG_CTX(("initializer:%s %s %s %d\n",
              md->anchor().data(),
              s->data(),
              md->getBodyDef()->getDefFileName().data(),
              md->getStartBodyLine()));
        QCString qsrc_refid = md->getOutputFileBase() + "_1" + md->anchor();
        struct Refid src_refid = insertRefid(qsrc_refid.data());
        struct Refid dst_refid = insertRefid(s->data());
        insertMemberReference(src_refid,dst_refid);
      }
      ++li;
    }
  }

  if ( md->getScopeString() )
  {
    bindTextParameter(memberdef_insert,":scope",md->getScopeString().data(),FALSE);
  }

  // +Brief, detailed and inbody description
  bindTextParameter(memberdef_insert,":briefdescription",md->briefDescription(),FALSE);
  bindTextParameter(memberdef_insert,":detaileddescription",md->documentation(),FALSE);
  bindTextParameter(memberdef_insert,":inbodydescription",md->inbodyDocumentation(),FALSE);

  // File location
  if (md->getDefLine() != -1)
  {
    int id_file = insertFile(stripFromPath(md->getDefFileName()));
    if (id_file!=-1)
    {
      bindIntParameter(memberdef_insert,":id_file",id_file);
      bindIntParameter(memberdef_insert,":line",md->getDefLine());
      bindIntParameter(memberdef_insert,":column",md->getDefColumn());

      if (md->getStartBodyLine()!=-1)
      {
        int id_bodyfile = insertFile(stripFromPath(md->getBodyDef()->absFilePath()));
        if (id_bodyfile == -1)
        {
            sqlite3_clear_bindings(memberdef_insert.stmt);
        }
        else
        {
            bindIntParameter(memberdef_insert,":id_bodyfile",id_bodyfile);
            bindIntParameter(memberdef_insert,":bodystart",md->getStartBodyLine());
            bindIntParameter(memberdef_insert,":bodyend",md->getEndBodyLine());
        }
      }
    }
  }

  int id_memberdef=step(memberdef_insert,TRUE);

  if (isFunc)
  {
    insertMemberFunctionParams(id_memberdef,md,def);
  }
  else if (md->memberType()==MemberType_Define &&
          md->argsString())
  {
    insertMemberDefineParams(id_memberdef,md,def);
  }

  // + source references
  // The cross-references in initializers only work when both the src and dst
  // are defined.
  MemberSDict *mdict = md->getReferencesMembers();
  if (mdict!=0)
  {
    MemberSDict::IteratorDict mdi(*mdict);
    MemberDef *rmd;
    for (mdi.toFirst();(rmd=mdi.current());++mdi)
    {
      insertMemberReference(md,rmd);//,mdi.currentKey());
    }
  }
  // + source referenced by
  mdict = md->getReferencedByMembers();
  if (mdict!=0)
  {
    MemberSDict::IteratorDict mdi(*mdict);
    MemberDef *rmd;
    for (mdi.toFirst();(rmd=mdi.current());++mdi)
    {
      insertMemberReference(rmd,md);//,mdi.currentKey());
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
  MemberDef *md;
  int count=0;
  for (mli.toFirst();(md=mli.current());++mli)
  {
    // namespace members are also inserted in the file scope, but
    // to prevent this duplication in the XML output, we filter those here.
    if (d->definitionType()!=Definition::TypeFile || md->getNamespaceDef()==0)
    {
      count++;
    }
  }
  if (count==0) return; // empty list
  for (mli.toFirst();(md=mli.current());++mli)
  {
    // namespace members are also inserted in the file scope, but
    // to prevent this duplication in the XML output, we filter those here.
    //if (d->definitionType()!=Definition::TypeFile || md->getNamespaceDef()==0)
    {
      generateSqlite3ForMember(md,d);
    }
  }
}


static void generateSqlite3ForClass(const ClassDef *cd)
{
  // + list of direct super classes
  // + list of direct sub classes
  // + include file
  // + list of inner classes
  // - template argument list(s)
  // + member groups
  // + list of all members
  // - brief description
  // - detailed description
  // - inheritance DOT diagram
  // - collaboration DOT diagram
  // - user defined member sections
  // - standard member sections
  // - detailed member documentation
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
  bindTextParameter(compounddef_insert,":kind",cd->compoundTypeString(),FALSE);
  bindIntParameter(compounddef_insert,":prot",cd->protection());

  int id_file = insertFile(stripFromPath(cd->getDefFileName()));
  bindIntParameter(compounddef_insert,":id_file",id_file);
  bindIntParameter(compounddef_insert,":line",cd->getDefLine());
  bindIntParameter(compounddef_insert,":column",cd->getDefColumn());

  step(compounddef_insert);

  // + list of direct super classes
  if (cd->baseClasses())
  {
    BaseClassListIterator bcli(*cd->baseClasses());
    BaseClassDef *bcd;
    for (bcli.toFirst();(bcd=bcli.current());++bcli)
    {
      int refid = insertRefid(bcd->classDef->getOutputFileBase());
      bindIntParameter(basecompoundref_insert,":refid", refid);
      bindIntParameter(basecompoundref_insert,":prot",bcd->prot);
      bindIntParameter(basecompoundref_insert,":virt",bcd->virt);

      if (!bcd->templSpecifiers.isEmpty())
      {
        bindTextParameter(basecompoundref_insert,":base",insertTemplateSpecifierInScope(bcd->classDef->name(),bcd->templSpecifiers),FALSE);
      }
      else
      {
        bindTextParameter(basecompoundref_insert,":base",bcd->classDef->displayName(),FALSE);
      }
      bindTextParameter(basecompoundref_insert,":derived",cd->displayName(),FALSE);
      step(basecompoundref_insert);
    }
  }

  // + list of direct sub classes
  if (cd->subClasses())
  {
    BaseClassListIterator bcli(*cd->subClasses());
    BaseClassDef *bcd;
    for (bcli.toFirst();(bcd=bcli.current());++bcli)
    {
      bindTextParameter(derivedcompoundref_insert,":base",cd->displayName(),FALSE);
      if (!bcd->templSpecifiers.isEmpty())
      {
        bindTextParameter(derivedcompoundref_insert,":derived",insertTemplateSpecifierInScope(bcd->classDef->name(),bcd->templSpecifiers),FALSE);
      }
      else
      {
        bindTextParameter(derivedcompoundref_insert,":derived",bcd->classDef->displayName(),FALSE);
      }
      int refid = insertRefid(bcd->classDef->getOutputFileBase());
      bindIntParameter(derivedcompoundref_insert,":refid", refid);
      bindIntParameter(derivedcompoundref_insert,":prot",bcd->prot);
      bindIntParameter(derivedcompoundref_insert,":virt",bcd->virt);
      step(derivedcompoundref_insert);
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
      int dst_id=insertFile(stripFromPath(nm));
      if (dst_id!=-1) {
        bindIntParameter(incl_select,":local",ii->local);
        bindIntParameter(incl_select,":src_id",id_file);
        bindIntParameter(incl_select,":dst_id",dst_id);
        int count=step(incl_select,TRUE,TRUE);
        if (count==0)
        {
          bindIntParameter(incl_insert,":local",ii->local);
          bindIntParameter(incl_insert,":src_id",id_file);
          bindIntParameter(incl_insert,":dst_id",dst_id);
          step(incl_insert);
        }
      }
    }
  }
  // + list of inner classes
  writeInnerClasses(cd->getClassSDict());

  // - template argument list(s)
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
}

static void generateSqlite3ForNamespace(const NamespaceDef *nd)
{
  // + contained class definitions
  // + contained namespace definitions
  // + member groups
  // + normal members
  // - brief desc
  // - detailed desc
  // - location
  // - files containing (parts of) the namespace definition

  if (nd->isReference() || nd->isHidden()) return; // skip external references

  // + contained class definitions
  writeInnerClasses(nd->getClassSDict());

  // + contained namespace definitions
  writeInnerNamespaces(nd->getNamespaceSDict());

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
  // - include graph
  // - included by graph
  // + contained class definitions
  // + contained namespace definitions
  // + member groups
  // + normal members
  // - brief desc
  // - detailed desc
  // - source code
  // - location
  // - number of lines

  if (fd->isReference()) return; // skip external references

  // + includes files
  IncludeInfo *ii;
  if (fd->includeFileList())
  {
    QListIterator<IncludeInfo> ili(*fd->includeFileList());
    for (ili.toFirst();(ii=ili.current());++ili)
    {
      int src_id=insertFile(stripFromPath(fd->absFilePath().data()));
      int dst_id=insertFile(stripFromPath(ii->includeName.data()));
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
      int src_id=insertFile(stripFromPath(ii->includeName));
      int dst_id=insertFile(stripFromPath(fd->absFilePath()));
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
    writeInnerClasses(fd->getClassSDict());
  }

  // + contained namespace definitions
  if (fd->getNamespaceSDict())
  {
    writeInnerNamespaces(fd->getNamespaceSDict());
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
#warning WorkInProgress
}

static void generateSqlite3ForDir(const DirDef *dd)
{
#warning WorkInProgress
}

// pages are just another kind of compound
// kinds of compound: class, struct, union, interface, protocol, category, exception, service, singleton, module, type, file, namespace, group, page, example, dir
static void generateSqlite3ForPage(const PageDef *pd,bool isExample)
{
#warning WorkInProgress
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

  // + classes
  ClassSDict::Iterator cli(*Doxygen::classSDict);
  ClassDef *cd;
  for (cli.toFirst();(cd=cli.current());++cli)
  {
    msg("Generating Sqlite3 output for class %s\n",cd->name().data());
    generateSqlite3ForClass(cd);
  }

  // + namespaces
  NamespaceSDict::Iterator nli(*Doxygen::namespaceSDict);
  NamespaceDef *nd;
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
    FileDef *fd;
    for (;(fd=fni.current());++fni)
    {
      msg("Generating Sqlite3 output for file %s\n",fd->name().data());
      generateSqlite3ForFile(fd);
    }
  }

  // + groups
  GroupSDict::Iterator gli(*Doxygen::groupSDict);
  GroupDef *gd;
  for (;(gd=gli.current());++gli)
  {
    msg("Generating Sqlite3 output for group %s\n",gd->name().data());
    generateSqlite3ForGroup(gd);
  }

  // + page
  {
    PageSDict::Iterator pdi(*Doxygen::pageSDict);
    PageDef *pd=0;
    for (pdi.toFirst();(pd=pdi.current());++pdi)
    {
      msg("Generating Sqlite3 output for page %s\n",pd->name().data());
      generateSqlite3ForPage(pd,FALSE);
    }
  }

  // + dirs
  {
    DirDef *dir;
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
    PageDef *pd=0;
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
