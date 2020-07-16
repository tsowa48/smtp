//#include <stdlib.h>
#include <string>//#include <string.h>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>//#include <threads.h>
#include <iostream>//#include <stdio.h>
#include <fcntl.h>
#include <postgresql/libpq-fe.h>

using namespace std;

static const string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static PGconn* dbConn = NULL;

static int is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

void log(const int level, const string msg) {
  const char* INFO = "\033[36m";
  const char* SUCCESS = "\033[32m";
  const char* WARNING = "\033[33m";
  const char* ERROR = "\033[31m";
  const char* RESET = "\033[0m";

  switch(level) {
    case 0://DEBUG
      break;
    case 1://INFO
      cout << INFO;
      break;
    case 2://SUCCESS
      cout << SUCCESS;
      break;
    case 3://WARNING
      cout << WARNING;
      break;
    case 4://ERROR
      cout << ERROR;
      break;
  }
  cout << "[ SMTP ] " << msg << RESET;
}

const char* b64_decode(string value) {
  int in_len = value.length();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  string ret;
  while (in_len-- && (value[in_] != '=') && is_base64(value[in_])) {
    char_array_4[i++] = value[in_]; in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);
      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
      for (i = 0; i < 3; i++)
        ret += char_array_3[i];
      i = 0;
    }
  }
  if (i) {
    for (j = i; j < 4; j++)
      char_array_4[j] = 0;
    for (j = 0; j < 4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);
    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
    for (j = 0; j < i - 1; j++) ret += char_array_3[j];
  }
  return ret.c_str();
}

class mail {
public:
  string from;
  string to;
  string data;

  mail() {}
};

static int sendmail(mail* msg) {
  const char* userParams[1];
  int rows;
  bool internalFrom = false;
  PGresult* res;

  userParams[0] = msg->from.c_str();
  res = PQexecParams(dbConn, "select U.id from public.users U, mail.domain D where\n"
                                       " (U.login || '@' || D.name) =\n"
                                       " coalesce((select A.destination from mail.aliases A\n"
                                       " where ($1::text like A.name and $1::text not in (select login || '@' || name from public.users, mail.domain))\n"
                                       " order by priority limit 1), $1::text)", 1, NULL, userParams, NULL, NULL, 0);
  rows = PQntuples(res);
  if(rows == 1) {
    internalFrom = true;
    char* uid = PQgetvalue(res, 0, 0);//res, row, col
    PQclear(res);
    const char* mailParams[3];
    mailParams[0] = uid;
    mailParams[1] = msg->to.c_str();
    mailParams[2] = msg->data.c_str();
    res = PQexecParams(dbConn, "insert into mail.outbox(\"from\", \"to\", \"message\") values ($1::bigint, $2::text, $3::text)", 3, NULL, mailParams, NULL, NULL, 0);
  }
  PQclear(res);

  userParams[0] = msg->to.c_str();
  res = PQexecParams(dbConn, "select U.id from public.users U, mail.domain D where\n"
                             " (U.login || '@' || D.name) =\n"
                             " coalesce((select A.destination from mail.aliases A\n"
                             " where ($1::text like A.name and $1::text not in (select login || '@' || name from public.users, mail.domain))\n"
                             " order by priority limit 1), $1::text)", 1, NULL, userParams, NULL, NULL, 0);
  rows = PQntuples(res);
  if(rows == 1) {
    char* uid = PQgetvalue(res, 0, 0);//res, row, col
    PQclear(res);
    const char* mailParams[3];
    mailParams[0] = uid;
    mailParams[1] = msg->from.c_str();
    mailParams[2] = msg->data.c_str();
    res = PQexecParams(dbConn, "insert into mail.inbox(\"to\", \"from\", \"message\") values ($1::bigint, $2::text, $3::text)", 3, NULL, mailParams, NULL, NULL, 0);
  } else {
    if(internalFrom) {
      //todo: send to external mail ??? (нет, конечно, не отправляем!!!)
    }
  }
  PQclear(res);
  return 0;
}

int smtp(const int sock, const string buf, mail* message) {
  string msg;
  string from;
  //char** rcpt = new char*[30];
  log(1, buf);
  if(buf.find("HELO") == 0) {
    message = new mail();
    msg = "250 gcg.name\n\0";
  } else if(buf.find("NOOP") == 0) {
    msg = "250 OK\n\0";
  } else if(buf.find("RSET") == 0) {
    message = new mail();
    msg = "250 OK\n\0";
  } else if(buf.find("EHLO") == 0) {
    message = new mail();
    msg = "250-gcg.name\n250-AUTH LOGIN\n250 HELP\n\0";
  } else if(buf.find("MAIL") == 0) {//MAIL FROM
    size_t mSp = buf.find('<') + 1;
    size_t mEp = buf.find('>', mSp);
    from = buf.substr(mSp, mEp - mSp);
    message->from = from;
    msg = "250 OK\n\0";
  } else if(buf.find("RCPT") == 0) {//RCPT TO
    size_t mSp = buf.find('<') + 1;
    size_t mEp = buf.find('>', mSp);
    string to = buf.substr(mSp, mEp - mSp);
    message->to = to;
    msg = "250 OK\n\0";
  } else if(buf.find("DATA") == 0) {
    msg = "354 SEND DATA\n\0";
    write(sock, msg.c_str(), msg.length());

    char* tmp = new char[1024];
    string data;
    int len;
    size_t endPos;
    while((len = recv(sock, tmp, 1024, 0)) > 0) {
      tmp[len] = '\0';
      data.append(tmp);
      delete[] tmp;
      if((endPos = data.rfind("\r\n.\r\n")) != string::npos) {
        log(4, "END Message\n");
        message->data = data.substr(0, endPos);
        break;
      }
      tmp = new char[1024];
    }
    msg = "250 QUEUED\n\0";
  } else if(buf.find("QUIT") == 0) {
    msg = "221 BYE\n";
    write(sock, msg.c_str(), msg.length());
    close(sock);
    return 0;
  //} else if(buf.find("AUTH") == 0) {//AUTH LOGIN
    //msg = "334 VXNlcm5hbWU6\n\0";
    //write(sock, msg.c_str(), msg.length());

    //string creds;

    //char* login = new char[255];
    //int len = recv(sock, login, -1, MSG_PEEK);
    //login = new char[len];
    //recv(sock, login, len, 0);
    //login[len - 2] = '\0';

    //creds.append(b64_decode(login));

    //msg = "334 UGFzc3dvcmQ6\n\0";
    //write(sock, msg.c_str(), msg.length());
    //char* password = new char[255];
    //len = recv(sock, password, -1, MSG_PEEK);
    //password = new char[len];
    //recv(sock, password, len, 0);
    //password[len - 2] = '\0';

    //creds.append("@");
    //creds.append(b64_decode(password));

    //log(1, string("credentials: ").append(creds));
    //todo: auth
    //msg = "235 AUTHENTICATED\n\0";
  } else {
    msg = "502 ERROR\n\0";
    log(3, string("unknown: ").append(buf));
  }
  write(sock, msg.c_str(), msg.length());
  //HELO <domain><CRLF>
  //EHLO <domain><CRLF>
  //MAIL FROM:<reverse-path><CRLF>
  //RCPT TO:<forward-path><CRLF>
  //DATA<CRLF>
  //RSET<CRLF>
  //SEND FROM:<reverse-path><CRLF>
  //SOML FROM:<reverse-path><CRLF>
  //SAML FROM:<reverse-path><CRLF>
  //VRFY <string><CRLF>
  //EXPN <string><CRLF>
  //HELP <string><CRLF>
  //NOOP<CRLF>
  //QUIT<CRLF>
  /*
  RSET - указвает серверу прервать выполнение текущего процесса. Все сохранённые данные (отправитель, получатель и др) удаляются. Сервер должен отправить положительный ответ.
  VRFY - просит сервер проверить, является ли переданный аргумент именем пользователя. В случае успеха сервер возвращает полное имя пользователя.
  EXPN - просит сервер подтвердить, что переданный аргумент - это список почтовой группы, и если так, то сервер выводит членов этой группы.
  HELP - запрашивает у сервера полезную помощь о переданной в качестве аргумента команде.
  */
  return 0;
}

void work(const int sock) {
  log(1, string("thread ").append(to_string(sock)).append("\n"));
  int sz = sizeof(int);
  int sockBufSize;
  getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&sockBufSize, (socklen_t*)&sz);
  //PGresult* result;
  char* tmp_buf = new char[sockBufSize];
  int len = 0;
  write(sock, "220 General Company Group ESMTP\n\0", 32);
  mail* message = new mail();
  while((len = recv(sock, tmp_buf, -1, MSG_PEEK)) > 0) {
    delete[] tmp_buf;
    char* buf = new char[len];
    //recv(sock, buf, len, 0);
    read(sock, buf, len);
    smtp(sock, buf, message);//SMTP Protocol
    if(message->from.length() > 3 && message->to.length() > 3 && message->data.length() > 1) {
      int result = sendmail(message);
      if(result == 0) {
        message = new mail();
      } else {
        log(4, "error sending message\n");
      }
    }
    delete[] buf;
    tmp_buf = new char[sockBufSize];
  }
  //delete[] tmp_buf;

  //result = PQexec(dbConn, "select 1");
  ////PQexecParams(dbConn, "select $1, $2", 2, NULL, params, NULL, NULL, 0);
  //delete[] tmp_buf;
  //PQclear(result);
  //result = NULL;
  log(1, string("end thread ").append(to_string(sock)).append("\n"));
}

int main() {
  const int PORT = 2525;
  int socket_desc;
  struct sockaddr_in server;
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(PORT);
  int counter = 11;
  while(bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0 && counter > 0) {
    log(4, "address in use\n");
    sleep(10);
    counter--;
  }
  if(counter == 0) {
    log(4, "error binding\n");
    return 1;
  }
  if(listen(socket_desc, 10) < 0) {
    log(4, "error listen\n");
    return 2;
  }
  log(2, "Started\n");
  int c = sizeof(struct sockaddr_in);

  fcntl(socket_desc, F_SETFL, O_NONBLOCK);
  dbConn = PQconnectdb("user=gcg password=gcg host=127.0.0.1 dbname=gcg");
  if(PQstatus(dbConn) != CONNECTION_OK) {
    log(4, "DB not connected\n");
    return 3;
  }


  PGresult* res = PQexec(dbConn, "select D.id, D.name from mail.domain D order by D.name");
  int rows = PQntuples(res);
  for(int i = 0; i < rows; ++i) {
    char* id = PQgetvalue(res, i, 0);
    char* name = PQgetvalue(res, i, 1);
    log(2, string("Domain ").append(name).append(" [").append(id).append("] loaded\n"));
  }
  PQclear(res);


  while(true) {
    struct sockaddr_in client;
    int client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
    if(client_sock < 0)
      continue;
    thread t(work, client_sock);
    t.detach();
  }
  PQfinish(dbConn);
  log(2, "Stopped\n");
  return 0;
}
