#include "../core/udt.h"

#include <iostream>
#include <signal.h>

#ifndef WIN32
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#endif

using namespace std;

#ifndef WIN32
void* monitor(void*);
#else
DWORD WINAPI monitor(LPVOID);
#endif

void* wait_for_new_result(void*);
void* send_things(void*);

void intHandler(int dummy) {
  //TODO (nathan jay): Print useful summary statistics.
  exit(0);
}

struct socket_couple {
  PccSender* pcc_sender1;
  PccSender* pcc_sender2;
  bool reversed;
};

pthread_mutex_t result_waiter_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char* argv[]) {
  if (argc < 4 || 0 == atoi(argv[3])) {
    cout << "usage: " << argv[0] << " <send|recv> server_ip server_port";
    cout << endl;
    return 0;
  }

  bool should_send = !strcmp(argv[1], "send");

  signal(SIGINT, intHandler);

  // use this function to initialize the UDT library
  UDT::startup();

  struct addrinfo hints, *local, *peer;

  memset(&hints, 0, sizeof(struct addrinfo));

  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (0 != getaddrinfo(NULL, "9000", &hints, &local)) {
    cout << "incorrect network address.\n" << endl;
    return 0;
  }

  UDTSOCKET client =
      UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);
  cout << "client1 id " << client << endl;

  UDTSOCKET client2 =
      UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);
  cout << "client2 id " << client2 << endl;

  PccSender* pcc_sender = UDT::get_pcc_sender(client);
  pcc_sender->id = 1;
  pcc_sender->set_rate(2097152);

  PccSender* pcc_sender2 = UDT::get_pcc_sender(client2);
  pcc_sender2->id = 2;
  pcc_sender2->set_rate(2097152*2);

#ifdef WIN32
  // Windows UDP issue
  // For better performance, modify HKLM\System\CurrentControlSet\Services\Afd\
  // \Parameters\FastSendDatagramThreshold
  UDT::setsockopt(client, 0, UDT_MSS, new int(1052), sizeof(int));
#endif

  freeaddrinfo(local);

  if (0 != getaddrinfo(argv[2], argv[3], &hints, &peer)) {
    cout << "incorrect server/peer address. " << argv[1] << ":" << argv[2];
    cout << endl;
    return 0;
  }

  // connect to the server, implict bind
  if (UDT::ERROR == UDT::connect(client, peer->ai_addr, peer->ai_addrlen)) {
    cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
    return 0;
  }
  // connect to the server, implict bind
  if (UDT::ERROR == UDT::connect(client2, peer->ai_addr, peer->ai_addrlen)) {
    cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
    return 0;
  }
  freeaddrinfo(peer);

  // using CC method
  int temp;

#ifndef WIN32
  pthread_create(new pthread_t, NULL, monitor, &client);
#else
  CreateThread(NULL, 0, monitor, &client, 0, NULL);
#endif

  if (should_send) {

    struct socket_couple sc1 = {pcc_sender, pcc_sender2, false};
    struct socket_couple sc2 = {pcc_sender2, pcc_sender, true};

    pthread_create(new pthread_t, NULL, wait_for_new_result, &sc1);
    pthread_create(new pthread_t, NULL, wait_for_new_result, &sc2);

    pthread_t sending_thread;
    pthread_t sending_thread2;
    pthread_create(&sending_thread, NULL, send_things, &client);
    pthread_create(&sending_thread2, NULL, send_things, &client2);
    void* val;
    void* val2;
    pthread_join(sending_thread, &val);
    pthread_join(sending_thread2, &val2);

  } else {
    int size = 100000;
    char* data = new char[size];
    bzero(data, size);

    while (true) {
      int rsize = 0;
      int rs;
      while (rsize < size) {
        if (UDT::ERROR ==
            (rs = UDT::recv(client, data + rsize, size - rsize, 0))) {
          cout << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
          break;
        }

        rsize += rs;
      }

      if (rsize < size) {
        break;
      }
    }
    UDT::close(client);
    delete [] data;
  }
  UDT::cleanup();

  return 1;
}

bool loss_in_both_once = false;
bool loss_again = false;

bool received_first = false;
bool received_second = false;

void* wait_for_new_result(void* arg) {

  struct socket_couple sc = *(socket_couple*) arg;

  PccSender* pcc_sender = sc.pcc_sender1;
  PccSender* pcc_sender2 = sc.pcc_sender2;
  bool reversed = sc.reversed;

  while (true) {
    pthread_mutex_lock(&(pcc_sender->new_result_mutex));
    while (!pcc_sender->just_got_new_result) {
      pthread_cond_wait(&(pcc_sender->new_result_cond), &(pcc_sender->new_result_mutex));

      pcc_sender->just_got_new_result = false;

      pthread_mutex_lock(&result_waiter_mutex);
      if (!reversed) {
        received_first = true;
      } else {
        received_second = true;
      }
      if (received_first && received_second) {
        received_first = false;
        received_second = false;

        PccSender* first_sender;
        PccSender* second_sender;

        if (!reversed) {
          first_sender = pcc_sender;
          second_sender = pcc_sender2;
        } else {
          first_sender = pcc_sender2;
          second_sender = pcc_sender;
        }
        bool loss_in_both = (first_sender->latest_utility_info_.actual_sending_rate > first_sender->latest_utility_info_.actual_good_sending_rate) && (second_sender->latest_utility_info_.actual_sending_rate > second_sender->latest_utility_info_.actual_good_sending_rate);

        if (loss_in_both_once && loss_in_both && !loss_again) {
          loss_again = loss_in_both;
        }
        if (!loss_in_both_once && loss_in_both) {
          loss_in_both_once = loss_in_both;
        }

        double throughput_rate_first = first_sender->latest_utility_info_.actual_good_sending_rate/first_sender->latest_utility_info_.actual_sending_rate;
        double throughput_rate_second = second_sender->latest_utility_info_.actual_good_sending_rate/second_sender->latest_utility_info_.actual_sending_rate;

        cout << "loss_once " << loss_in_both_once << " loss_again " << loss_again << " throughput_rate_first " << throughput_rate_first << " throughput_rate_second " << throughput_rate_second << " loss_ratio " << throughput_rate_first/throughput_rate_second << endl;

        if (!loss_again) {
          first_sender->set_rate(first_sender->sending_rate_ * 2);
          second_sender->set_rate(second_sender->sending_rate_ * 2);
        }
      }
      pthread_mutex_unlock(&result_waiter_mutex);

    }
    pthread_mutex_unlock(&(pcc_sender->new_result_mutex));
  }
}

void* send_things(void* arg) {
  UDTSOCKET client = *(UDTSOCKET*) arg;

  int size = 100000;
  char* data = new char[size];
  bzero(data, size);

  while (true) {
    int ssize = 0;
    int ss;
    while (ssize < size) {
      if (UDT::ERROR ==
          (ss = UDT::send(client, data + ssize, size - ssize, 0))) {
        cout << "send:" << UDT::getlasterror().getErrorMessage() << endl;
        break;
      }

      ssize += ss;
    }

    if (ssize < size) {
      break;
    }
  }
  UDT::close(client);
}

#ifndef WIN32
void* monitor(void* s)
#else
DWORD WINAPI monitor(LPVOID s)
#endif
{
  UDTSOCKET u = *(UDTSOCKET*)s;

  UDT::TRACEINFO perf;

  // cout << "SendRate(Mb/s)\tRTT(ms)\tCTotal\tLoss\tRecvACK\tRecvNAK" << endl;
  cout << "SendRate(Mb/s)\tRTT(ms)\tCTotal\tLoss" << endl;
  int i=0;
  while (true) {
#ifndef WIN32
    usleep(1000000);
#else
    Sleep(1000);
#endif
    i++;
    if (UDT::ERROR == UDT::perfmon(u, &perf)) {
      cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
      break;
    }
    cout << i << "\t" << perf.mbpsSendRate << "\t" << perf.msRTT << "\t"
         << perf.pktSentTotal << "\t" << perf.pktSndLossTotal << endl;
  }

#ifndef WIN32
  return NULL;
#else
  return 0;
#endif
}

