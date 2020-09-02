#include "../core/udt.h"

#include <iostream>
#include <iomanip>
#include <signal.h>

#ifndef WIN32
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
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
void* monitor2(void*);

void intHandler(int dummy) {
  //TODO (nathan jay): Print useful summary statistics.
  exit(0);
}

struct socket_couple {
  PccSender* pcc_sender1;
  PccSender* pcc_sender2;
  int connection_id_sender1;
  int connection_id_sender2;
  bool reversed;
};

struct monitor_struct {
  UDTSOCKET socket;
  int64_t interval;
  string file_name;
};

#define MIN_RATE 100000.0
pthread_mutex_t result_waiter_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t sending_thread;
pthread_t sending_thread2;

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
  pcc_sender->set_rate(MIN_RATE);

  PccSender* pcc_sender2 = UDT::get_pcc_sender(client2);
  pcc_sender2->id = 2;
  pcc_sender2->set_rate(MIN_RATE*2);

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

#ifndef WIN32
  pthread_create(new pthread_t, NULL, monitor, &client);
  char* file_name_char_array = getenv("file_name_for_logging");
  if (file_name_char_array) {
    string file_name_string = string(file_name_char_array);
    struct monitor_struct* mc = new monitor_struct();
    mc->socket = client;
    mc->interval = 10000;
    mc->file_name = file_name_string;
    pthread_create(new pthread_t, NULL, monitor2, mc);
  }
#else
  CreateThread(NULL, 0, monitor, &client, 0, NULL);
#endif


  if (should_send) {

    struct socket_couple sc1 = {pcc_sender, pcc_sender2, client, client2, false};
    struct socket_couple sc2 = {pcc_sender2, pcc_sender, client2, client, true};

    pthread_create(new pthread_t, NULL, wait_for_new_result, &sc1);
    pthread_create(new pthread_t, NULL, wait_for_new_result, &sc2);

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

// bool loss_in_both_once = false;
bool loss_again = false;

bool received_first = false;
bool received_second = false;

UtilityInfo utility_info_first;
UtilityInfo utility_info_second;

void* wait_for_new_result(void* arg) {

  struct socket_couple sc = *(socket_couple*) arg;

  PccSender* pcc_sender = sc.pcc_sender1;
  PccSender* pcc_sender2 = sc.pcc_sender2;
  bool reversed = sc.reversed;

  while (true) {
    while (!pcc_sender->just_got_new_result) {
      pthread_mutex_lock(&(pcc_sender->new_result_mutex));
      pthread_cond_wait(&(pcc_sender->new_result_cond), &(pcc_sender->new_result_mutex));
      if (loss_again) {
        pthread_mutex_unlock(&(pcc_sender->new_result_mutex));
        return NULL;
      }
    }
    pcc_sender->just_got_new_result = false;

    pthread_mutex_lock(&result_waiter_mutex);

    if (!reversed) {
      received_first = true;
      utility_info_first = pcc_sender->latest_utility_info_;
    } else {
      received_second = true;
      utility_info_second = pcc_sender->latest_utility_info_;
    }

    if (received_first && received_second) {
      received_first = false;
      received_second = false;

      PccSender* first_sender;
      PccSender* second_sender;

      // int first_connection_id = -1;
      int second_connection_id = -1;

      if (!reversed) {
        first_sender = pcc_sender;
        second_sender = pcc_sender2;
        // first_connection_id = sc.connection_id_sender1;
        second_connection_id = sc.connection_id_sender2;
      } else {
        first_sender = pcc_sender2;
        second_sender = pcc_sender;
        // first_connection_id = sc.connection_id_sender2;
        second_connection_id = sc.connection_id_sender1;
      }
      // bool loss_in_both = (utility_info_first.actual_sending_rate > utility_info_first.actual_good_sending_rate) && (utility_info_second.actual_sending_rate > utility_info_second.actual_good_sending_rate);
      bool loss_in_both = (first_sender->latest_utility_info_.actual_sending_rate > first_sender->latest_utility_info_.actual_good_sending_rate) && (second_sender->latest_utility_info_.actual_sending_rate > second_sender->latest_utility_info_.actual_good_sending_rate);

      // bool loss_in_one = (first_sender->latest_utility_info_.actual_sending_rate > first_sender->latest_utility_info_.actual_good_sending_rate) || (second_sender->latest_utility_info_.actual_sending_rate > second_sender->latest_utility_info_.actual_good_sending_rate);

      // if (!loss_in_both) {
      //   loss_in_both_once = false;
      //   loss_again = false;
      // }

      double maximum_goodput;
      if (first_sender->latest_utility_info_.utility != 1 && second_sender->latest_utility_info_.utility != 1 && first_sender->lost_at_least_one_packet_already && second_sender->lost_at_least_one_packet_already && loss_in_both && !loss_again) {
        loss_again = loss_in_both;
        maximum_goodput = first_sender->latest_utility_info_.actual_good_sending_rate+second_sender->latest_utility_info_.actual_good_sending_rate;
      }

      // double throughput_rate_first = utility_info_first.actual_good_sending_rate/utility_info_first.actual_sending_rate;
      // double throughput_rate_second = utility_info_second.actual_good_sending_rate/utility_info_second.actual_sending_rate;
      double throughput_rate_first = first_sender->latest_utility_info_.actual_good_sending_rate/first_sender->latest_utility_info_.actual_sending_rate;
      double throughput_rate_second = second_sender->latest_utility_info_.actual_good_sending_rate/second_sender->latest_utility_info_.actual_sending_rate;

      double loss_ratio = throughput_rate_first/throughput_rate_second;
      cout << "reversed " << reversed << " loss_again " << loss_again << " throughput_rate_first " << throughput_rate_first << " throughput_rate_second " << throughput_rate_second << " loss_ratio " << loss_ratio << endl;

      double new_first_rate = first_sender->sending_rate_;
      double new_second_rate = second_sender->sending_rate_;
      // if (!loss_in_both) {
      if ((!(first_sender->lost_at_least_one_packet_already && second_sender->lost_at_least_one_packet_already)) || (first_sender->lost_at_least_one_packet_already && second_sender->lost_at_least_one_packet_already && first_sender->latest_utility_info_.utility != 1 && second_sender->latest_utility_info_.utility != 1 && !loss_again)) {
        new_first_rate *= 2;
        new_second_rate *= 2;
      }

      first_sender->set_rate(new_first_rate);
      second_sender->set_rate(new_second_rate);

      if (loss_again) {
        first_sender->set_rate(maximum_goodput);

        // pthread_cancel(sending_thread2);
        UDT::close(second_connection_id);
        // second_sender->set_rate(0);
        if ((loss_ratio > 1.5 || getenv("START_VEGAS")) && !getenv("START_PCC_CLASSIC")) {
          cout << "Starting Vegas" << endl;
          first_sender->set_vegas();
        } else {
          cout << "Starting PCC Classic" << endl;
          first_sender->set_pcc_classic();
        }
      }
    }
    pthread_mutex_unlock(&result_waiter_mutex);
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
  return 0;
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

void* monitor2(void* arg)
{
  assert(arg != NULL);
  struct monitor_struct mc = *(monitor_struct*) arg;

  UDTSOCKET u = mc.socket;
  int64_t interval = mc.interval;
  string file_name = mc.file_name;
  cout << "Logging to file name " << file_name << endl;

  UDT::TRACEINFO perf;
  ofstream logfile;
  logfile.open(file_name);

  logfile << "i,timestamp,RTT(ms)" << endl;
  for (size_t i=0;;i++) {
    usleep(interval);
    i++;
    if (UDT::ERROR == UDT::perfmon(u, &perf)) {
      cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
      break;
    }
    auto current_time = std::chrono::system_clock::now();
    auto duration_in_seconds = std::chrono::duration<double>(current_time.time_since_epoch());
    double num_seconds = duration_in_seconds.count();

    logfile << i << "," << fixed << setprecision(numeric_limits<long double>::digits10 + 1) << num_seconds << "," << perf.msRealRTT << endl;
  }

  return NULL;
}

