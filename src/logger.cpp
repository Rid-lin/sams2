/***************************************************************************
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include <syslog.h>
#include <iostream>
#include <sstream>
#include <time.h>

#include "config.h"

#ifdef USE_UNIXODBC
#include "odbcconn.h"
#include "odbcquery.h"
#endif

#ifdef USE_MYSQL
#include "mysqlconn.h"
#include "mysqlquery.h"
#endif

#ifdef USE_PQ
#include "pgconn.h"
#include "pgquery.h"
#endif

#include "logger.h"
#include "tools.h"
#include "samsconfig.h"
#include "debug.h"

bool Logger::_started = true;
bool Logger::_verbose = false;
uint Logger::_dbgLevel = 0;
string Logger::_sender = "logger";
Logger::LoggerEngine Logger::_engine = OUT_CONSOLE;
ofstream Logger::_fout;
DBConn *Logger::_conn = NULL;
bool Logger::_connection_owner = false;

void Logger::sendInfo (const string & mess)
{
  if (!_verbose)
    return;

  switch (_engine)
    {
    case OUT_CONSOLE:
      cout << mess << endl;
      break;
    case OUT_FILE:
      _fout << mess << endl;
      break;
    case OUT_SYSLOG:
      syslog (LOG_INFO, "%s", mess.c_str ());
      break;
    default:
      break;
    }
}


void Logger::sendDebug (uint level, const string & mess)
{
  if ((_dbgLevel == 0) || (_dbgLevel < level))
    return;

  switch (_engine)
    {
    case OUT_CONSOLE:
      cout << mess << endl;
      break;
    case OUT_FILE:
      _fout << mess << endl;
      break;
    case OUT_SYSLOG:
      syslog (LOG_DEBUG, "%s", mess.c_str ());
      break;
    default:
      break;
    }
}


void Logger::sendWarning (const string & mess)
{
  switch (_engine)
    {
    case OUT_CONSOLE:
      cerr << mess << endl;
      break;
    case OUT_FILE:
      _fout << mess << endl;
      break;
    case OUT_SYSLOG:
      syslog (LOG_WARNING, "%s", mess.c_str ());
      break;
    default:
      break;
    }
}


void Logger::sendError (const string & mess)
{
  switch (_engine)
    {
    case OUT_CONSOLE:
      cerr << mess << endl;
      break;
    case OUT_FILE:
      _fout << mess << endl;
      break;
    case OUT_SYSLOG:
      syslog (LOG_ERR, "%s", mess.c_str ());
      break;
    default:
      break;
    }
}

void Logger::setSender(const string & sender)
{
  _sender = sender;
}

bool Logger::setEngine (const string & engine)
{
  stop ();

  vector < string > tblOptions;

  if (engine == "syslog")
    {
      openlog (_sender.c_str(), LOG_PID | LOG_CONS, LOG_DAEMON);
      _engine = OUT_SYSLOG;
      _started = true;
    }
  else if (engine == "console")
    {
      _engine = OUT_CONSOLE;
      _started = true;
    }
  else if (engine.find ("file") >= 0)
    {
      string fname;

      Split (engine, ":", tblOptions);
      if (tblOptions.size () == 2)
        fname = tblOptions[1];
      else
        fname = _sender + ".log";

      _fout.open (fname.c_str (), ios::out);

      _started = true;

      if (!_fout.is_open ())
        {
          _engine = OUT_CONSOLE;
          sendError ("Unable to open file " + fname);
        }
      else
        {
          _engine = OUT_FILE;
        }
    }

  return _started;
}


void Logger::setDebugLevel (uint level)
{
  _dbgLevel = level;
}


void Logger::setVerbose (bool verbose)
{
  _verbose = verbose;
}


void Logger::stop ()
{
  if (!_started)
    return;

  switch (_engine)
    {
    case OUT_CONSOLE:
      break;
    case OUT_FILE:
      _fout.close ();
      break;
    case OUT_SYSLOG:
      closelog ();
      break;
    default:
      break;
    }
  _started = false;
}


void Logger::useConnection (DBConn * conn)
{
  if (_conn)
    {
      DEBUG (DEBUG_LOGGER, "[" << __FUNCTION__ << "] Already using " << _conn);
      return;
    }
  if (conn)
    {
      DEBUG (DEBUG_LOGGER, "[" << __FUNCTION__ << "] Using external connection " << conn);
      _conn = conn;
      _connection_owner = false;
    }
}

void Logger::destroy()
{
  if (_connection_owner && _conn)
    {
      DEBUG (DEBUG_LOGGER, "[" << __FUNCTION__ << "] Destroy connection " << _conn);
      delete _conn;
      _conn = NULL;
    }
  else if (_conn)
    {
      DEBUG (DEBUG_LOGGER, "[" << __FUNCTION__ << "] Not owner for connection " << _conn);
    }
  else
    {
      DEBUG (DEBUG_LOGGER, "[" << __FUNCTION__ << "] Not connected");
    }
  stop ();
}


void Logger::addLog(LogKind code, const string &mess)
{
  if (!_conn)
    {
      DBConn::DBEngine engine = SamsConfig::getEngine();

      if (engine == DBConn::DB_UODBC)
        {
          #ifdef USE_UNIXODBC
          _conn = new ODBCConn();
          #else
          return;
          #endif
        }
      else if (engine == DBConn::DB_MYSQL)
        {
          #ifdef USE_MYSQL
          _conn = new MYSQLConn();
          #else
          return;
          #endif
        }
      else if (engine == DBConn::DB_PGSQL)
        {
          #ifdef USE_PQ
          _conn = new PgConn();
          #else
          return;
          #endif
        }
      else
        return;

      if (!_conn->connect ())
        {
          delete _conn;
          return;
        }
      _connection_owner = true;
    }

  DBQuery *query = NULL;

  if (_conn->getEngine() == DBConn::DB_UODBC)
    {
      #ifdef USE_UNIXODBC
      query = new ODBCQuery((ODBCConn*)_conn);
      #else
      return;
      #endif
    }
  else if (_conn->getEngine() == DBConn::DB_MYSQL)
    {
      #ifdef USE_MYSQL
      query = new MYSQLQuery((MYSQLConn*)_conn);
      #else
      return;
      #endif
    }
  else if (_conn->getEngine() == DBConn::DB_PGSQL)
    {
      #ifdef USE_PQ
      query = new PgQuery((PgConn*)_conn);
      #else
      return;
      #endif
    }
  else
    return;

  time_t now;
  char str_today[15];
  char str_now[15];
  basic_stringstream < char >sqlcmd;

  now = time (NULL);
  strftime (str_today, sizeof (str_today), "%Y-%m-%d", localtime (&now));
  strftime (str_now, sizeof (str_now), "%H:%M:%S", localtime (&now));

  sqlcmd << "insert into samslog (s_issuer, s_date, s_time, s_value, s_code)";
  sqlcmd << " VALUES ('" << _sender << "', '" << str_today << "', '" << str_now << "', '" << mess << "', '" << code << "')";
  query->sendQueryDirect (sqlcmd.str ());

  delete query;
}

