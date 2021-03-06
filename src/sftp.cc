/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */
 
#include "sftp.h"
#include <fcntl.h>
#include <node_buffer.h>

#define SFTP_MIN(a,b) ((a) < (b) ? (a) : (b))
#define SFTP_MAX_ERROR 4096
#define SFTP_BUFFER_SIZE 4096

Persistent<FunctionTemplate> SFTP::constructor_template;
Persistent<ObjectTemplate> SFTP::data_template;

static Persistent<FunctionTemplate> stats_constructor_template;

static Persistent<String> dev_symbol;
static Persistent<String> ino_symbol;
static Persistent<String> mode_symbol;
static Persistent<String> nlink_symbol;
static Persistent<String> uid_symbol;
static Persistent<String> gid_symbol;
static Persistent<String> rdev_symbol;
static Persistent<String> size_symbol;
static Persistent<String> blksize_symbol;
static Persistent<String> blocks_symbol;
static Persistent<String> atime_symbol;
static Persistent<String> mtime_symbol;
static Persistent<String> ctime_symbol;


// node.js like fs.stat object it's used for stat
Local<Object> BuildStatsObject(sftp_attributes a) {
  HandleScope scope;

  if (mode_symbol.IsEmpty()) {
    dev_symbol = NODE_PSYMBOL("dev");
    ino_symbol = NODE_PSYMBOL("ino");
    mode_symbol = NODE_PSYMBOL("mode");
    nlink_symbol = NODE_PSYMBOL("nlink");
    uid_symbol = NODE_PSYMBOL("uid");
    gid_symbol = NODE_PSYMBOL("gid");
    rdev_symbol = NODE_PSYMBOL("rdev");
    size_symbol = NODE_PSYMBOL("size");
    blksize_symbol = NODE_PSYMBOL("blksize");
    blocks_symbol = NODE_PSYMBOL("blocks");
    atime_symbol = NODE_PSYMBOL("atime");
    mtime_symbol = NODE_PSYMBOL("mtime");
    ctime_symbol = NODE_PSYMBOL("ctime");
  }

  Local<Object> stats =
    stats_constructor_template->GetFunction()->NewInstance();
  stats->Set(mode_symbol, Integer::New(a->permissions));
  stats->Set(uid_symbol, Integer::New(a->uid));
  stats->Set(gid_symbol, Integer::New(a->gid));
  stats->Set(size_symbol, Number::New(a->size));
  stats->Set(atime_symbol, NODE_UNIXTIME_V8(a->atime));
  stats->Set(mtime_symbol, NODE_UNIXTIME_V8(a->mtime));
  stats->Set(ctime_symbol, NODE_UNIXTIME_V8(a->createtime));
  return scope.Close(stats);
}

/***********************************************
*                 SFTP class
************************************************/

SFTP::SFTP(const Arguments& args) 
  : SSHBase(args)
  , m_sftp_session(NULL)  
  , m_buf(NULL)
  , m_buf2(NULL)
  , m_list(NULL)
  , m_timer(NULL)
{
  m_list = new ListNode();
  m_list->data = 0;
}

SFTP::~SFTP()
{  
  freeSessions();
  resetData();
}

Handle<Value> SFTP::New(const Arguments &args) 
{
  // instantiation    
  HandleScope scope;
  SFTP *sftp = new SFTP(args);
  sftp->Wrap(args.This());
  return args.This();
}

void SFTP::resetData()
{
  // reseting all the data stored on the SFTP object    
  SSHBase::resetData();
  if (m_buf)
    free(m_buf);
  if (m_buf2)
    free(m_buf2);
  if (m_list->next) {
    delete m_list->next;
    m_list->next = NULL;
  } 
  m_killed = 0;

  m_buf = m_buf2 = NULL;
  m_size = m_pos = m_done = 0;
}

void SFTP::freeSessions()
{
  // releasing libssh realted resources    
  if (m_sftp_session) {
    sftp_free(m_sftp_session);  
    m_sftp_session = NULL;
  }  
  SSHBase::freeSessions();
}

int SFTP::startConnect(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  // ssh connection (connect/auth)
  if (!SSHBase::startConnect(req))
    return 0;
  
  // sftp session creation + init
  if (!pthis->m_sftp_session)
    pthis->m_sftp_session = sftp_new(pthis->m_ssh_session);  
  if (!pthis->m_sftp_session) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Could not create sftp session \n");  
    return 0;
  }  

  int res = sftp_init(pthis->m_sftp_session);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Error initializing SFTP session: %d.\n", 
      sftp_get_error(pthis->m_sftp_session));
    return 0;
  }

  return 0;
}

int SFTP::startMkdir(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  mode_t mode = static_cast<mode_t>(pthis->m_int);
  int res = sftp_mkdir(pthis->m_sftp_session, pthis->m_buf, S_IRWXU);
  if (res != SSH_OK) {
    if (sftp_get_error(pthis->m_sftp_session) != SSH_FX_FILE_ALREADY_EXISTS) {
      snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't create directory: %s\n", 
        ssh_get_error(pthis->m_ssh_session));
        return 0;
    }
  }
  return 0;
}

int SFTP::startWriteFile(eio_req *req)
{    
  SFTP* pthis = (SFTP*) req->data;
  
  int access_type = O_WRONLY | O_CREAT | O_TRUNC;
  
  // opening the file here but doing the actual write in chunks in continueWriteFile
  pthis->m_sftp_file = sftp_open(pthis->m_sftp_session, 
    pthis->m_buf, access_type, S_IRWXU);
  if (pthis->m_sftp_file == NULL) {
    pthis->m_done = 1;
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't open file for writing: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return SSH_ERROR;
  }
  
  return 0;
}

int SFTP::cbWriteFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;  
  // if done let's finish the task
  if (pthis->m_done) {
    pthis->onDone(req);
  }
  else {
    // else write next chunk  
    eio_custom(continueWriteFile, EIO_PRI_DEFAULT, cbWriteFile, pthis);
  }  
  return 0; 
}

int SFTP::continueWriteFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  int res,nwritten;
  int next_chunk = SFTP_MIN(pthis->m_size - pthis->m_pos, SFTP_BUFFER_SIZE);
  // write next chunk
  nwritten = sftp_write(pthis->m_sftp_file, 
    pthis->m_wdata+pthis->m_pos, next_chunk);
  if (nwritten != next_chunk) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't write data to file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    pthis->m_done = 1;
    sftp_close(pthis->m_sftp_file);
    return 0;
  }
  // update position in the 'to-write' buffer
  pthis->m_pos+=next_chunk;
  
  // if there is more to write let's return and another call will follow soon
  if (pthis->m_pos != pthis->m_size)  
    return 0;
    
  // if no more data to be written, close the file    
  pthis->m_done = 1;

  res = sftp_close(pthis->m_sftp_file);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't close the written file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
  }
  return 0;  
}

int SFTP::startReadFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int access_type;

  // let's open the file for reading
  access_type = O_RDONLY;
  pthis->m_sftp_file = sftp_open(pthis->m_sftp_session, 
    pthis->m_buf, access_type, 0);
  if (pthis->m_sftp_file == NULL) {
    pthis->m_done = 1;
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't open file for reading: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }
  return 0;
}

int SFTP::cbReadFile(eio_req *req) {
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;  
  
  if (pthis->m_done) {
    // if we are done with the reading let's finish the task  
    pthis->onDone(req);
  }
  else {
    // else read next chunk  
    eio_custom(continueReadFile, EIO_PRI_DEFAULT, cbReadFile, pthis);
  }  
  return 0;
}

int SFTP::continueReadFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  int res, nwritten, nbytes;
  char buffer[SFTP_BUFFER_SIZE];

  // read next chunk
  nbytes = sftp_read(pthis->m_sftp_file, buffer, sizeof(buffer));
  if (nbytes < 0) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Error while reading file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    sftp_close(pthis->m_sftp_file);
    pthis->m_done = 1;
    return 0;
  }

  // write it into the temporary local file
  if (write(pthis->m_int, buffer, nbytes) != nbytes) {
    sftp_close(pthis->m_sftp_file);
    pthis->m_done = 1;
    return 0;
  }

  // if there is more to read let's return and another call will follow soon
  if (nbytes > 0) 
    return 0;

  // if we read the whole file let's close it
  pthis->m_done = 1;
  res = sftp_close(pthis->m_sftp_file);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't close the read file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }

  return 0;
}

int SFTP::startListDir(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  sftp_dir dir;
  sftp_attributes attributes;
  int res;
  // start reading the dir
  dir = sftp_opendir(pthis->m_sftp_session, pthis->m_buf);
  if (!dir) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Directory not opened: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }

  // loop over the entries, store the names and stats of the entries
  while ((attributes = sftp_readdir(pthis->m_sftp_session, dir)) != NULL) {
     if (strcmp(attributes->name,".")!=0 
      && strcmp(attributes->name,"..")!=0)
          pthis->m_list->add(attributes->name, attributes);
     else
      sftp_attributes_free(attributes);
  }

  if (!sftp_dir_eof(dir)) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't list directory: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    sftp_closedir(dir);
    return 0;
  }

  // stop the dir reading
  res = sftp_closedir(dir);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't close directory: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }  
  return 0;
}

int SFTP::startRename(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int res = sftp_rename(pthis->m_sftp_session, pthis->m_buf, pthis->m_buf2);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't rename file/directory: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
      return 0;
  }
  return 0;
}

int SFTP::startChmod(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  mode_t mode = static_cast<mode_t>(pthis->m_int);
  int res = sftp_chmod(pthis->m_sftp_session, pthis->m_buf, mode);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "chmod failed: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }
  return 0;
}

int SFTP::startChown(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int res = sftp_chown(pthis->m_sftp_session, 
    pthis->m_buf, pthis->m_int, pthis->m_int2);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "chown failed: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }
  return 0;
}

int SFTP::startStat(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;

  pthis->m_stat = sftp_stat(pthis->m_sftp_session, pthis->m_buf);
  if (!pthis->m_stat) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "stat failed: %s\n", 
      ssh_get_error(pthis->m_ssh_session)); 
    return 0;
  }
  return 0;
}

int SFTP::startUnlink(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int res = sftp_unlink(pthis->m_sftp_session, pthis->m_buf);
  if (res != SSH_OK) {
    if (sftp_get_error(pthis->m_sftp_session) != SSH_FX_FILE_ALREADY_EXISTS) {
      snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't remove file: %s\n", 
        ssh_get_error(pthis->m_ssh_session));      
      return 0;
    }
  }
  return 0;
}

int SFTP::startRmdir(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int res = sftp_rmdir(pthis->m_sftp_session, pthis->m_buf);
  if (res != SSH_OK) {
    if (sftp_get_error(pthis->m_sftp_session) != SSH_FX_FILE_ALREADY_EXISTS) {
      snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't remove directory: %s\n", 
        ssh_get_error(pthis->m_ssh_session));
      return 0;
    }
  }
  return 0;
}

int SFTP::startSpawn(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int res;
  // creating and initialising a channel for the shell command
  pthis->m_ssh_channel = ssh_channel_new(pthis->m_ssh_session);
  if (!pthis->m_ssh_channel) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't create shh channel.\n");
    pthis->m_done = 1;
    return 0;
  }

  res = ssh_channel_open_session(pthis->m_ssh_channel);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't open shh channel.\n");
    ssh_channel_free(pthis->m_ssh_channel);
    pthis->m_ssh_channel = NULL;
    pthis->m_done = 1;
    return 0;
  }

  res = ssh_channel_request_pty(pthis->m_ssh_channel);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't request pty.\n");
    ssh_channel_free(pthis->m_ssh_channel);
    pthis->m_ssh_channel = NULL;
    pthis->m_done = 1;
    return 0;
  }

  // sending the command to the remote shell
  res = ssh_channel_request_exec(pthis->m_ssh_channel, pthis->m_buf);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "SSH exec failed.\n");
    ssh_channel_close(pthis->m_ssh_channel);
    ssh_channel_free(pthis->m_ssh_channel);
    pthis->m_ssh_channel = NULL;
    pthis->m_done = 1;
    return 0;
  }
  
  // allocating space for the read buffers (stdout, stderr of the remote proc.)
  free(pthis->m_buf);
  pthis->m_buf = (char*)malloc(SFTP_BUFFER_SIZE);
  pthis->m_buf2 = (char*)malloc(SFTP_BUFFER_SIZE);
  pthis->m_size = pthis->m_size2 = 0;
  return 0;
}

int SFTP::continueSpawn(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;          
  
  int available, available2;
  pthis->m_size = pthis->m_size2 = 0;
  if (pthis->m_killed) {
    // if kill command has been called, then this should be the last round  
    pthis->m_done = 1;
    return 0;
  }
  
  int eof = 0;
  // poll and read if available stdout
  available = ssh_channel_poll(pthis->m_ssh_channel, 0);
  if (available == SSH_EOF) {
    // end of file -> we still need to read the data that is available...
    eof = 1;
    available = SFTP_BUFFER_SIZE;
  }    
  if (available < 0) 
    // something went wrong... error report below
    pthis->m_done = 1;
  else if (available > 0) {
    // read what we can  
    pthis->m_size = ssh_channel_read(pthis->m_ssh_channel,
      pthis->m_buf,SSH_MIN(available, SFTP_BUFFER_SIZE),0);
	if (pthis->m_size < 0) {
		// if error, then end
        pthis->m_size = 0;
        pthis->m_done = 1;
	} 
  }
  
  // poll and read if available stderr, same logic as above
  available2 = ssh_channel_poll(pthis->m_ssh_channel, 1);
  if (available2 == SSH_EOF) 
    available2 = SFTP_BUFFER_SIZE;
  if (available2 < 0)
    pthis->m_done = 1;
  if (available2 > 0) {
    pthis->m_size2 = ssh_channel_read(pthis->m_ssh_channel,
      pthis->m_buf2,SSH_MIN(available2, SFTP_BUFFER_SIZE),1);
    if (pthis->m_size2 < 0) {
      pthis->m_size2 = 0;
      pthis->m_done = 1;          
    }  
  }
  
  if (pthis->m_size < 0 || pthis->m_size2 < 0) {
    // something went wront, let's report the error
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "SSH exec read error.%s\n", 
      ssh_get_error(pthis->m_ssh_session));
    pthis->m_done = 1;
    return 0;
  }
  
  if (eof && !pthis->m_size && !pthis->m_size2) {
    // end of file, nothing else to read on either channel, we are done and happy
    pthis->m_done = 1;
  }
  
  return 0;
}

int SFTP::cbSpawn(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;    
  if (pthis->m_size) {
    // read something from stdout, let's emitt an 'stdout' event  
    Handle<Value> argv[1];
    argv[0] = createBuffer(pthis->m_buf, pthis->m_size);  
    pthis->Emit(stdout_symbol, 1, argv);
  }
  if (pthis->m_size2) {
    // read something from stderr, let's emitt an 'stderr' event  
    Handle<Value> argv[1];
    argv[0] = createBuffer(pthis->m_buf2, pthis->m_size2);  
    pthis->Emit(stderr_symbol, 1, argv);
  }  
  
  if (pthis->m_done) {
    // are we done? let's emit the final 'done' event  
    if (pthis->m_timer) {
        ev_timer_stop(pthis->m_timer);
        delete pthis->m_timer;
        pthis->m_timer = NULL;
    }    
    pthis->onExit(req);
  }
  else {
    if (pthis->m_size || pthis->m_size2) {
        // if something was on the channel to read let's continue reading
        eio_custom(continueSpawn, EIO_PRI_DEFAULT, cbSpawn, pthis);        
    }
    else {
        // else, let's set up a timer and poll the channel later
        if (!pthis->m_timer) {
            pthis->m_timer = new ev_timer();
            pthis->m_timer->data = pthis;
            ev_timer_init(pthis->m_timer, timerSpawn, 0., 0.1);
        }
        
        ev_timer_again(pthis->m_timer);
    }
  }  
  return 0; 
}

void SFTP::timerSpawn (ev_timer *w, int revents)
{
    // after timeout set in cbSpawn, let's continue polling
    eio_custom(continueSpawn, EIO_PRI_DEFAULT, SFTP::cbSpawn, w->data);
}

int SFTP::onExit(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;
  Handle<Value> argv[2];
  int exit = -1;
  if (pthis->m_ssh_channel) {    
    // send eof + close the channel + get exit code
    if (!pthis->m_error)
      ssh_channel_send_eof(pthis->m_ssh_channel);
    ssh_channel_close(pthis->m_ssh_channel);
    exit = ssh_channel_get_exit_status(pthis->m_ssh_channel);
    ssh_channel_free(pthis->m_ssh_channel);
    pthis->m_ssh_channel = NULL;
  }  
  argv[0] = Integer::New(exit); 
  argv[1] = String::New(pthis->m_error);
  pthis->Emit(callback_symbol, 2, argv);
  ev_unref(EV_DEFAULT_UC);
  return 0;
}

int SFTP::onStat(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;
  Handle<Value> argv[2];
  //fprintf(stderr, pthis->m_error);
  argv[0] = String::New(pthis->m_error);  
  //fprintf(stderr, "mstat: %p\n", pthis->m_stat);
  if (pthis->m_stat) {
    argv[1] = BuildStatsObject(pthis->m_stat);
  }
  pthis->Emit(callback_symbol, pthis->m_stat ? 2 : 1, argv);    
  sftp_attributes_free(pthis->m_stat);
  pthis->m_stat = 0;  
  ev_unref(EV_DEFAULT_UC);
  return 0;
}

int SFTP::onList(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;
  Handle<Value> argv[3];
  argv[0] = String::New(pthis->m_error);
  v8::Local<v8::Array> res = v8::Array::New();
  v8::Local<v8::Array> stats = v8::Array::New();
  ListNode* it = pthis->m_list;
  for (int i=0; it->next; i++) {
    it = it->next;
    res->Set(Number::New(i), String::New(it->data)); 
    if (it->attr) {
      stats->Set(Number::New(i), BuildStatsObject(it->attr));       
    }
  }
  argv[1] = res;
  argv[2] = stats;
  pthis->Emit(callback_symbol, 3, argv);
  ev_unref(EV_DEFAULT_UC);
  return 0;
}

void SFTP::Initialize(Handle<Object>& target)
{
    HandleScope scope;
    static int inited = 0;
    if (!inited) {
      // let's init the constructor function     
      Local<FunctionTemplate> t = FunctionTemplate::New(New);
      constructor_template = Persistent<FunctionTemplate>::New(t);
      // inherit from EventEmitter
      constructor_template->Inherit(EventEmitter::constructor_template);
      constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
      // set class name
      constructor_template->SetClassName(String::NewSymbol("SFTP"));
      data_template = Persistent<ObjectTemplate>::New(
        ObjectTemplate::New());
      
      // add functions to the prototype (exporting the c++ functions)
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "init", init);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "connect", connect);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "mkdir", mkdir);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "writeFile", writeFile);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "readFile", readFile);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "listDir", listDir);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "rename", rename);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "chmod", chmod);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "chown", chown);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "stat", stat);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "unlink", unlink);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "rmdir", rmdir);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "spawn", spawn);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "setPubKey", SSHBase::setPubKey);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "setPrvKey", SSHBase::setPrvKey);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "isConnected", isConnected);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "interrupt", SSHBase::interrupt);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "kill", kill);
      
      // symbols used for the events we emitt, are static
      callback_symbol = NODE_PSYMBOL("callback");
      stdout_symbol = NODE_PSYMBOL("stdout");;
      stderr_symbol = NODE_PSYMBOL("stderr");;
      
      // stats helper class
      stats_constructor_template = Persistent<FunctionTemplate>::New(
        FunctionTemplate::New());

      inited = true;
    }
    
    // setting constructor methods on the main exported object (see init.cc)
    target->Set(
        String::NewSymbol("SFTP"),
        constructor_template->GetFunction()
    );
    
    target->Set(
        String::NewSymbol("Stats"),
        stats_constructor_template->GetFunction()
    );
}

Handle<Value> SFTP::init(const Arguments &args) 
{
  // init for the ssh conenction (domain, user, keys, etc...)    
  return SSHBase::init(args);
}

Handle<Value> SFTP::connect(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This());
  pthis->resetData();  
  eio_custom(startConnect, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC);
    
  return True();
}

Handle<Value> SFTP::mkdir(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  pthis->m_int = args[1]->Int32Value();
  eio_custom(startMkdir, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> SFTP::writeFile(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  Local<Object> buffer_obj = args[1]->ToObject();
  pthis->m_size = Buffer::Length(buffer_obj);
  pthis->m_wdata = (char*) malloc (pthis->m_size);
  memcpy(pthis->m_wdata, Buffer::Data(buffer_obj), pthis->m_size);
  
  eio_custom(startWriteFile, EIO_PRI_DEFAULT, cbWriteFile, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> SFTP::readFile(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->m_int = args[0]->Int32Value();
  setCharData(pthis->m_buf, args[1]);
  eio_custom(startReadFile, EIO_PRI_DEFAULT, cbReadFile, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> SFTP::listDir(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  eio_custom(startListDir, EIO_PRI_DEFAULT, onList, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::rename(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  setCharData(pthis->m_buf2, args[1]);
  eio_custom(startRename , EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::chmod(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  pthis->m_int = args[1]->Int32Value();
  eio_custom(startChmod , EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::chown(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  pthis->m_int = args[1]->Int32Value(); // uid
  pthis->m_int = args[2]->Int32Value(); // gid
  eio_custom(startChown , EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::stat(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  eio_custom(startStat , EIO_PRI_DEFAULT, onStat, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::unlink(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  eio_custom(startUnlink, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::rmdir(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  eio_custom(startRmdir, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::spawn(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_buf, args[0]);
  eio_custom(startSpawn, EIO_PRI_DEFAULT, cbSpawn, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::isConnected(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  int ret = ssh_is_connected(pthis->m_ssh_session);
  //fprintf(stdout, "isconnected: %d\n", ret);
  return ret == 1 ? True() : False();
}

Handle<Value>SFTP::kill(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->m_killed = 1;
  return True();
}

/***********************************************
*           ListNode helper class
************************************************/

// this helper class is used for storing the entries of the list directory
// it's a simplified linked list

void ListNode::add(const char* data, sftp_attributes attr) {
  ListNode* last = this;
  while(last->next)
    last = last->next;
  last = last->next = new ListNode();
  last->data = strdup(data);
  last->attr = attr;
  last->next = 0;
}

ListNode::ListNode() 
  : data(NULL), next(NULL), attr(NULL)
{}

ListNode::~ListNode() {
  if (data)
    free(data);
  if (next)
    delete next;
  if (attr)
    sftp_attributes_free(attr);
}


