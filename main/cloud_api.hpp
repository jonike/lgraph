
#include "lgraph.hpp"
#include "main_api.hpp"

class Cloud_api {
protected:
  static void server(Eprp_var &var) {

    const std::string host = var.get("host","localhost");
    const std::string port = var.get("port","80");

    Main_api::warn(fmt::format("Still not implemented host:{} port:{}", host, port));
  }

  Cloud_api() {
  }
public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("cloud.server", "setup a lgshell server", &Cloud_api::server);
    m1.add_label_optional("host","host name for the cloud setup");
    m1.add_label_optional("port","port to use for the http request");

    eprp.register_method(m1);
  }

};
