#include "catch.hpp"
#include "boson/boson.h"
#include <unistd.h>
#include <iostream>
#include "boson/logger.h"
#include "boson/semaphore.h"
#include "boson/select.h"

using namespace boson;
using namespace std::literals;

TEST_CASE("Routines - I/O", "[routines][i/o]") {
  boson::debug::logger_instance(&std::cout);

  SECTION("Simple pipes") {
    int pipe_fds[2];
    ::pipe(pipe_fds);

    boson::run(1, [&]() {
      start(
          [](int in) -> void {
            size_t data;
            int result = boson::read(in, &data, sizeof(size_t), 5ms);
            CHECK(result == boson::code_timeout);
            result = boson::read(in, &data, sizeof(size_t));
            CHECK(0 < result);
          },
          pipe_fds[0]);

      start(
          [](int out) -> void {
            size_t data{0};
            boson::sleep(10ms);
            int result = boson::write(out, &data, sizeof(data));
            CHECK(0 < result);
          },
          pipe_fds[1]);
    });

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
  }
}

TEST_CASE("Routines - Select", "[routines][i/o][select]") {
  boson::debug::logger_instance(&std::cout);

  SECTION("Simple pipes - reads") {
    int pipe_fds1[2];
    ::pipe(pipe_fds1);
    int pipe_fds2[2];
    ::pipe(pipe_fds2);

    boson::run(1, [&]() {
      boson::channel<std::nullptr_t,1> tickets;
      start(
          [](int in1, int in2, auto tickets) -> void {
            int result = 0;
            size_t data;
            result = boson::select_any(                 //
                event_read(in1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(in2, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 2;  //
                           }));
            CHECK(result == 2);
            tickets << nullptr;
            result = boson::select_any(                 //
                event_read(in1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(in2, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 2;  //
                           }));
            CHECK(result == 1);
          },
          pipe_fds1[0], pipe_fds2[0], tickets);

      start(
          [](int out1, int out2, auto tickets) -> void {
            std::nullptr_t sink;
            size_t data{0};
            boson::write(out2, &data, sizeof(data));
            tickets >> sink; // Wait for other routin to finish first test
            boson::write(out1, &data, sizeof(data));
          },
          pipe_fds1[1], pipe_fds2[1], tickets);
    });

    ::close(pipe_fds1[0]);
    ::close(pipe_fds1[1]);
    ::close(pipe_fds2[0]);
    ::close(pipe_fds2[1]);
  }

  SECTION("Simple pipes - writes") {
    int pipe_fds1[2];
    ::pipe(pipe_fds1);
    int pipe_fds2[2];
    ::pipe(pipe_fds2);

    boson::run(1, [&]() {
      boson::channel<std::nullptr_t,1> tickets;
      start(
          [](int in1, int in2, auto tickets) -> void {
            size_t data{0};
            boson::read(in1, &data, sizeof(size_t));
            boson::read(in2, &data, sizeof(size_t));
          },
          pipe_fds1[0], pipe_fds2[0], tickets);

      start(
          [](int in1, int int2, int out1, int out2, auto tickets) -> void {
            std::nullptr_t sink;
            size_t data{0};
            int result = boson::select_any(                 //
                event_write(out1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_write(out2, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 2;  //
                           }));
            CHECK(result == 1);
            result = boson::select_any(                 //
                event_write(in1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_write(out2, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 2;  //
                           }));
            CHECK(result == 2);
          },
          pipe_fds1[0], pipe_fds2[0], pipe_fds1[1], pipe_fds2[1], tickets);
    });

    ::close(pipe_fds1[0]);
    ::close(pipe_fds1[1]);
    ::close(pipe_fds2[0]);
    ::close(pipe_fds2[1]);
  }

  SECTION("Channels") {
    boson::run(1, [&]() {
      boson::channel<std::nullptr_t,1> tickets;
      boson::channel<std::nullptr_t,1> tickets1;
      boson::channel<std::nullptr_t,1> tickets2;
      start(
          [](auto t1, auto t2, auto tickets) -> void {
            int result = 0;
            size_t data;
            int chandata;
            result = boson::select_any(                 //
                event_read(t1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(t2, chandata,  //
                           []() {
                             return 2;  //
                           }));
            CHECK(result == 2);
            result = boson::select_any(                 //
                event_read(t1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(t2, chandata,  //
                           []() {
                             return 2;  //
                           }));
            CHECK(result == 2);
            tickets << nullptr;
            result = boson::select_any(                 //
                event_read(t1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(t2, chandata,  //
                           []() {
                             return 2;  //
                           }));
            CHECK(result == 1);
          },
          t1, t2, tickets);

      start(
          [](auto t1, auto t2, auto tickets) -> void {
            std::nullptr_t sink;
            size_t data{0};
            out2 << 1;
            tickets >> sink; // Wait for other routin to finish first test
            boson::write(out1, &data, sizeof(data));
          },
          t1, t2, tickets);
    });
  }

  SECTION("Pipe and channels") {
    int pipe_fds1[2];
    ::pipe(pipe_fds1);

    boson::run(1, [&]() {
      boson::channel<std::nullptr_t,5> tickets;
      boson::channel<int,5> pipe2;
      pipe2 << 1;
      start(
          [](int in1, auto in2, auto tickets) -> void {
            int result = 0;
            size_t data;
            int chandata;
            result = boson::select_any(                 //
                event_read(in1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(in2, chandata,  //
                           []() {
                             return 2;  //
                           }));
            CHECK(result == 2);
            result = boson::select_any(                 //
                event_read(in1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(in2, chandata,  //
                           []() {
                             return 2;  //
                           }));
            CHECK(result == 2);
            tickets << nullptr;
            result = boson::select_any(                 //
                event_read(in1, &data, sizeof(size_t),  //
                           [](ssize_t) {
                             return 1;  //
                           }),
                event_read(in2, chandata,  //
                           []() {
                             return 2;  //
                           }));
            CHECK(result == 1);
          },
          pipe_fds1[0], pipe2, tickets);

      start(
          [](int out1, auto out2, auto tickets) -> void {
            std::nullptr_t sink;
            size_t data{0};
            out2 << 1;
            tickets >> sink; // Wait for other routin to finish first test
            boson::write(out1, &data, sizeof(data));
          },
          pipe_fds1[1], pipe2, tickets);
    });

    ::close(pipe_fds1[0]);
    ::close(pipe_fds1[1]);
  }
}
