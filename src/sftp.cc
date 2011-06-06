#include "sftp.h"
#include <fcntl.h>
#include <node_buffer.h>

#define SFTP_MIN(a,b) ((a) < (b) ? (a) : (b))
#define SFTP_MAX_ERROR 4096

Persistent<FunctionTemplate> SFTP::constructor_template;
Persistent<ObjectTemplate> SFTP::data_template;
Persistent<String> SFTP::callback_symbol;

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

SFTP::SFTP(const Arguments &) 
  : m_ssh_session(NULL)
  , m_sftp_session(NULL)  
  , m_error(NULL)
  , m_path(NULL)
  , m_path2(NULL)
  , m_list(NULL)
  , m_wdata(NULL)  
  , m_pub_key(NULL)
  , m_prv_key(NULL)
{
  m_list = new ListNode();
  m_list->data = 0;
  m_error = (char*) malloc (SFTP_MAX_ERROR);
  //m_data = Persistent<Object>::New(data_template->NewInstance());
}

SFTP::~SFTP()
{  
  freeSessions();
  resetData();
  if (m_pub_key)
    ssh_string_free(m_pub_key);
  if (m_prv_key)
    privatekey_free(m_prv_key);
  //m_data.Dispose();
}

Handle<Value> SFTP::New(const Arguments &args) 
{  
  HandleScope scope;
  SFTP *sftp = new SFTP(args);
  sftp->Wrap(args.This());
  return args.This();
}

void SFTP::setCharData(char*& to, Local<Value> data)
{
  String::Utf8Value str(Local<Object>::Cast(data));
  to = strdup(*str);
}

void SFTP::resetData()
{
  if (m_error) 
    m_error[0] = (char) 0;
  if (m_path)
    free(m_path);
  if (m_path2)
    free(m_path2);
  if (m_list->next) {
    delete m_list->next;
    m_list->next = NULL;
  } 
  if (m_wdata)
   free(m_wdata);
  m_path = m_path2 = m_wdata = NULL;
  m_size = m_pos = m_done = 0;
}

void SFTP::freeSessions()
{
  if (m_sftp_session) {
    sftp_free(m_sftp_session);  
    m_sftp_session = NULL;
  }  
  if (m_ssh_session) {
    ssh_disconnect(m_ssh_session);
    ssh_free(m_ssh_session);
    m_ssh_session = NULL;
  }
}

int SFTP::startConnect(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  if (ssh_is_connected(pthis->m_ssh_session)) {
     ssh_disconnect(pthis->m_ssh_session);   
  }
  int res = ssh_connect(pthis->m_ssh_session);
  if (res != SSH_OK) {
    fprintf(stderr, "connection failed\n");
    snprintf(pthis->m_error, SFTP_MAX_ERROR, 
            "Error connecting: %s\n",
            ssh_get_error(pthis->m_ssh_session));
    return 0;
  }
  fprintf(stderr, "after connection\n");
  // TODO: ssh_is_server_known() and warn the user if not...
  
  //res = ssh_userauth_password(pthis->m_ssh_session, NULL, *password);
  if (pthis->m_prv_key && pthis->m_pub_key) {
    res = ssh_userauth_pubkey (pthis->m_ssh_session,
      NULL, pthis->m_pub_key, pthis->m_prv_key);	
    
    if (res != SSH_AUTH_SUCCESS)  {
      snprintf(pthis->m_error, SFTP_MAX_ERROR, 
        "Error authenticating with keys: %s\n",
        ssh_get_error(pthis->m_ssh_session));
      return 0;        
    }  
  }  
  else {
    res = ssh_userauth_autopubkey(pthis->m_ssh_session, NULL);
    
    if (res != SSH_AUTH_SUCCESS)  {
      snprintf(pthis->m_error, SFTP_MAX_ERROR, 
        "Error authenticating with password: %s\n",
        ssh_get_error(pthis->m_ssh_session));
      return 0;        
    }
  }
  
  if (!pthis->m_sftp_session)
    pthis->m_sftp_session = sftp_new(pthis->m_ssh_session);  
  if (!pthis->m_sftp_session) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Could not create sftp session \n");  
    return 0;
  }  

  res = sftp_init(pthis->m_sftp_session);
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
  int res = sftp_mkdir(pthis->m_sftp_session, pthis->m_path, S_IRWXU);
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
  
  pthis->m_sftp_file = sftp_open(pthis->m_sftp_session, 
    pthis->m_path, access_type, S_IRWXU);
  if (pthis->m_sftp_file == NULL) {
    pthis->m_done = 1;
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't open file for writing: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return SSH_ERROR;
  }
  
  return 0;
}

int SFTP::continueWriteFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  int res,nwritten;
  int next_chunk = SFTP_MIN(pthis->m_size - pthis->m_pos, 1024);
//  fprintf(stderr, "from:%d fd:%d l:%d ", pthis->m_wdata+pthis->m_pos, pthis->m_sftp_file, next_chunk);
  nwritten = sftp_write(pthis->m_sftp_file, 
    pthis->m_wdata+pthis->m_pos, next_chunk);
//  fprintf(stderr, "return: %d\n", nwritten);  
  if (nwritten != next_chunk) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't write data to file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    pthis->m_done = 1;
    sftp_close(pthis->m_sftp_file);
    return 0;
  }
  pthis->m_pos+=next_chunk;
  if (pthis->m_pos != pthis->m_size)  
    return 0;
    
  pthis->m_done = 1;

  res = sftp_close(pthis->m_sftp_file);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't close the written file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
  }
  //fprintf(stderr, "write done\n");
  return 0;  
}

int SFTP::cbWriteFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;  
  
  if (pthis->m_done) {
    pthis->onDone(req);
  }
  else {
    eio_custom(continueWriteFile, EIO_PRI_DEFAULT, cbWriteFile, pthis);
  }  
  return 0; 
}

int SFTP::startReadFile(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  int access_type;

  access_type = O_RDONLY;
  pthis->m_sftp_file = sftp_open(pthis->m_sftp_session, 
    pthis->m_path, access_type, 0);
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
    //fprintf(stderr, "read done\n");
    pthis->onDone(req);
  }
  else {
    eio_custom(continueReadFile, EIO_PRI_DEFAULT, cbReadFile, pthis);
  }  
  return 0;
}

int SFTP::continueReadFile(eio_req *req)
{
  //fprintf(stderr, "read cont\n");
  SFTP* pthis = (SFTP*) req->data;
  int res, nwritten, nbytes;
  char buffer[1024*10];

  nbytes = sftp_read(pthis->m_sftp_file, buffer, sizeof(buffer));
  if (nbytes < 0) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Error while reading file: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    sftp_close(pthis->m_sftp_file);
    pthis->m_done = 1;
    return 0;
  }

  if (write(pthis->m_int, buffer, nbytes) != nbytes) {
    sftp_close(pthis->m_sftp_file);
    pthis->m_done = 1;
    return 0;
  }

  if (nbytes > 0)
    return 0;

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

  dir = sftp_opendir(pthis->m_sftp_session, pthis->m_path);
  if (!dir) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Directory not opened: %s\n", 
      ssh_get_error(pthis->m_ssh_session));
    return 0;
  }

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
  
  int res = sftp_rename(pthis->m_sftp_session, pthis->m_path, pthis->m_path2);
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
  int res = sftp_chmod(pthis->m_sftp_session, pthis->m_path, mode);
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
    pthis->m_path, pthis->m_int, pthis->m_int2);
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

  pthis->m_stat = sftp_stat(pthis->m_sftp_session, pthis->m_path);
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
  
  int res = sftp_unlink(pthis->m_sftp_session, pthis->m_path);
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
  
  int res = sftp_rmdir(pthis->m_sftp_session, pthis->m_path);
  if (res != SSH_OK) {
    if (sftp_get_error(pthis->m_sftp_session) != SSH_FX_FILE_ALREADY_EXISTS) {
      snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't remove directory: %s\n", 
        ssh_get_error(pthis->m_ssh_session));
      return 0;
    }
  }
  return 0;
}

int SFTP::startExec(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  ssh_channel channel;
  int res;
  channel = ssh_channel_new(pthis->m_ssh_session);
  if (!channel) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't create shh channel.");
    return 0;
  }

  res = ssh_channel_open_session(channel);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't open shh channel.");
    ssh_channel_free(channel);
    return 0;
  }
  
  res = ssh_channel_request_exec(channel, pthis->m_path);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "SSH exec failed.");
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    return 0;
  }
  
  char buffer[1024];
  unsigned int nbytes;

  nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0);
  while (nbytes > 0) {
    pthis->m_list->add(buffer);
    nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0);
  }

  if (nbytes < 0) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "SSH exec read error.");
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    return 0;
  }
  
  ssh_channel_send_eof(channel);
  ssh_channel_close(channel);
  ssh_channel_free(channel);
  return 0;
}

int SFTP::startSetPubKey(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;

  pthis->m_pub_key = publickey_from_file(pthis->m_ssh_session, 
    pthis->m_path, NULL);  	
  if (!pthis->m_pub_key) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't set public key.");
  }
  return 0;
}

int SFTP::startSetPrvKey(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  
  pthis->m_prv_key = privatekey_from_file(pthis->m_ssh_session, 
    pthis->m_path, 0, NULL);		
  if (!pthis->m_prv_key) {
    snprintf(pthis->m_error, SFTP_MAX_ERROR, "Can't set private key.");
  }
  return 0;
}

int SFTP::onDone(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;
  Handle<Value> argv[1];
  argv[0] = String::New(pthis->m_error);
  pthis->Emit(callback_symbol, 1, argv);
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
  fprintf(stderr, "mstat: %p\n", pthis->m_stat);
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
/*
int SFTP::onList(eio_req *req)
{
  SFTP* pthis = (SFTP*) req->data;
  HandleScope scope;
  Handle<Value> argv[3];
  argv[0] = String::New(pthis->m_error);
  v8::Local<v8::Array> res = v8::Array::New();
  v8::Local<v8::Array> attr = v8::Array::New();
  ListNode* it = pthis->m_list;
  for (int i=0; it->next; i++) {
    it = it->next;
    res->Set(Number::New(i), String::New(it->data)); 
  }
  argv[1] = res;
  argv[2] = attr;
  pthis->Emit(callback_symbol, 3, argv);
  ev_unref(EV_DEFAULT_UC);
  return 0;
}
*/
void SFTP::Initialize(Handle<Object>& target)
{
    HandleScope scope;
    static int inited = 0;
    if (!inited) {
      Local<FunctionTemplate> t = FunctionTemplate::New(New);
      constructor_template = Persistent<FunctionTemplate>::New(t);
      constructor_template->Inherit(EventEmitter::constructor_template);
      constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
      constructor_template->SetClassName(String::NewSymbol("SFTP"));
      data_template = Persistent<ObjectTemplate>::New(
        ObjectTemplate::New());
      
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
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "exec", exec);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "setPubKey", setPubKey);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "setPrvKey", setPrvKey);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "isConnected", isConnected);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "interrupt", interrupt);
      callback_symbol = NODE_PSYMBOL("callback");
      
      stats_constructor_template = Persistent<FunctionTemplate>::New(
        FunctionTemplate::New());

      inited = true;
    }
    
    target->Set(
        String::NewSymbol("SFTP"),
        constructor_template->GetFunction()
    );
    
    target->Set(
        String::NewSymbol("Stats"),
        stats_constructor_template->GetFunction()
    );
}

inline void setOption(ssh_session& session, Local<Object>& obj, 
                      const char* prop_name, ssh_options_e opt) 
{
  Local<Object> prop = Local<Object>::Cast(
      obj->Get(String::NewSymbol(prop_name))
  );
  if (!prop->IsUndefined()) {
      Local<String> value = prop->ToString();
      char *cvalue = new char[ value->Length() + 1 ];
      value->WriteAscii(cvalue); 
      ssh_options_set(session, opt, cvalue);
      delete cvalue;
  }
}

Handle<Value> SFTP::init(const Arguments &args) 
{  
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This());
  pthis->freeSessions();
  
  pthis->m_ssh_session = ssh_new();
  if (!pthis->m_ssh_session)
    return False();
  
  Local<Object> opt = Local<Object>::Cast(args[0]);    
  setOption(pthis->m_ssh_session, opt, "host", SSH_OPTIONS_HOST);
  setOption(pthis->m_ssh_session, opt, "port", SSH_OPTIONS_PORT_STR);
  setOption(pthis->m_ssh_session, opt, "user", SSH_OPTIONS_USER);
  ssh_options_set(pthis->m_ssh_session, SSH_OPTIONS_TIMEOUT, (void*) new long(1));
  ssh_options_set(pthis->m_ssh_session, SSH_OPTIONS_LOG_VERBOSITY, (void*) new int(SSH_LOG_PACKET));
  return True();  
}

Handle<Value> SFTP::connect(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This());
  pthis->resetData();
  // TODO: key validation
  //String::Utf8Value password(args[0]->ToString());
  //String::Utf8Value pub_key(args[1]->ToString());
  
  eio_custom(startConnect, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC);
    
  return True();
}

Handle<Value> SFTP::mkdir(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
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
  pthis->setCharData(pthis->m_path, args[0]);
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
  pthis->setCharData(pthis->m_path, args[1]);
  eio_custom(startReadFile, EIO_PRI_DEFAULT, cbReadFile, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> SFTP::listDir(const Arguments &args) 
{
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startListDir, EIO_PRI_DEFAULT, onList, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::rename(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  pthis->setCharData(pthis->m_path2, args[1]);
  eio_custom(startRename , EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::chmod(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  pthis->m_int = args[1]->Int32Value();
  eio_custom(startChmod , EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::chown(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
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
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startStat , EIO_PRI_DEFAULT, onStat, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::unlink(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startUnlink, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::rmdir(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startRmdir, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::exec(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startExec, EIO_PRI_DEFAULT, onList, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::setPubKey(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startSetPubKey, EIO_PRI_DEFAULT, onList, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::setPrvKey(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  pthis->resetData();
  pthis->setCharData(pthis->m_path, args[0]);
  eio_custom(startSetPrvKey, EIO_PRI_DEFAULT, onList, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value>SFTP::isConnected(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  int ret = ssh_is_connected(pthis->m_ssh_session);
  fprintf(stdout, "isconnected: %d\n", ret);
  return ret == 1 ? True() : False();
}

Handle<Value>SFTP::interrupt(const Arguments &args){
  HandleScope scope;
  SFTP *pthis = ObjectWrap::Unwrap<SFTP>(args.This()); 
  fprintf(stderr, "interrupt\n");
  //ssh_disconnect(pthis->m_ssh_session);
  //ssh_silent_disconnect(pthis->m_ssh_session);
  ssh_set_fd_except(pthis->m_ssh_session);
  pthis->freeSessions();
  fprintf(stderr, "interrupted\n");
  return True();
}

/***********************************************
*           ListNode helper class
************************************************/

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


