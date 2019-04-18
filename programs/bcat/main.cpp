#include <fc/io/json.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/thread/future.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include "json.hpp"
#include "readline.hpp"
#include "syntax_visitor.hpp"

namespace cr = readlinepp;
using ret = cr::readline::return_code;

class ws_client
{
  public:
    explicit ws_client(const std::string &url, std::string user, std::string pass)
      : m_user(std::move(user))
      , m_pass(std::move(pass))
    {
      init(url);
    }

    unsigned info(const std::string &, const std::vector<std::string> &)
    {
      std::cout << "Welcome to the simple ws bc client.\n";
      return ret::Ok;
    }

    unsigned handle_command(const fc::variants &syntax_description, const std::string &help, const std::string &api, const std::vector<std::string> &input)
    {
      if (input.size() - 1 != syntax_description.size())
      {
        std::cout << "Usage: " << input[0] << " ";
        for (const auto &item: syntax_description)
          std::cout << item.as_string() << " ";
        std::cout << "\n";
        return 1;
      }
      std::ostringstream args{"[", std::ios_base::ate};
      for (unsigned int i = 1; i < input.size(); ++i)
      {
        args << input[i];
        if (i != input.size() - 1)
          args << ", ";
      }
      args << "]";
      try
      {
        auto x = qd_json::parse(args.str());
        qd_json::visitor v;
        boost::apply_visitor(v, x);
        if (check_syntax(v.get_variant(), syntax_description))
          send_call_and_print_result(input[0], api, v.get_variant());
      }
      catch (std::runtime_error &e)
      {
        std::cout << e.what() << "\n";
      }
      return 0;
    }

  protected:
    void init(const std::string &url)
    {
      try
      {
        m_conn = m_client.connect(url);
        m_conn->on_message_handler([&](const std::string& s){
          m_promise->set_value(s);
        });
        // fixme: refactor this foo
        send_call("login", fc::variants{m_user, m_pass}, 1, [this](const std::string &res){
          std::cout << res << "\n";
          fc::variant r = fc::json::from_string(res);
          if (r.is_object())
          {
            fc::variant_object o = r.get_object();
            if (o.contains("result") && o["result"].as_bool())
            {
              send_call("database", fc::variants{}, 1, [this](const std::string &res2){
                std::cout << res2 << "\n";
                fc::variant r2 = fc::json::from_string(res2);
                if (r2.is_object())
                {
                  fc::variant_object o2 = r2.get_object();
                  if (o2.contains("result")) {
                    m_db_api = static_cast<uint32_t >(o2["result"].as_int64());
                    send_call("history", fc::variants{}, 1, [this](const std::string &res3) {
                      std::cout << res3 << "\n";
                      fc::variant r3 = fc::json::from_string(res3);
                      if (r3.is_object())
                      {
                        fc::variant_object o3 = r3.get_object();
                        if (o3.contains("result"))
                          m_history_api = static_cast<uint32_t >(o3["result"].as_int64());
                      }
                    });
                  }
                }
              });
            }
          }
        });
      }
      catch (fc::exception &e)
      {
        std::cout << "cannot connect to '" << "'" << url << "\n";
        std::exit(-1);
      }
    }

    void send_call(const std::string &cmd, const fc::variant &v1, uint32_t id, const std::function<void(const std::string &result)> &completer)
    {
      fc::mutable_variant_object o;
      fc::variants v2{id, cmd, v1};
      o("id", m_cnt++)("method", "call")("params", v2);
      std::cout << "sent: '" << fc::json::to_string(o) << "'\n";
      m_conn->send_message(fc::json::to_string(o));
      m_promise = new fc::promise<std::string>();
      std::string s = fc::future<std::string>(m_promise).wait();
      completer(s);
    }

    void send_call_and_print_result(const std::string &cmd, const std::string &api, const fc::variant &v1)
    {
      uint32_t api_id = api == "database" ? m_db_api : m_history_api;
      send_call(cmd, v1, api_id, [](const std::string &result) {
        std::cout << "got: '" << result << "'\n";
      });
    }

    bool check_syntax(const fc::variant &input, const fc::variants &syntax_description)
    {
      syntax_visitor visitor(syntax_description);
      try
      {
        for (const auto &item: input.get_array())
          item.visit(visitor);
        return true;
      }
      catch (std::runtime_error &e)
      {
        std::cout << e.what() << "\n";
      }
      return false;
    }

    uint32_t m_cnt = 0;
    uint32_t m_db_api;
    uint32_t m_history_api;
    std::string m_user;
    std::string m_pass;
    fc::http::websocket_client m_client;
    fc::http::websocket_connection_ptr m_conn;
    fc::promise<std::string>::ptr m_promise;
};

void configure_readline(const std::string &config, ws_client &ws, cr::readline &rl)
{
  try
  {
    fc::variant conf = fc::json::from_file(config);
    if (conf.is_array())
    {
      const fc::variants &arr = conf.get_array();
      for (const auto &item : arr)
      {
        if (!item.is_object())
        {
          std::cout << "error reading config file, expected an array of objects!\n";
          std::exit(-1);
        }
        const fc::variant_object &obj = item.get_object();
        std::string api{"database"};
        if (obj.contains("name") && obj.contains("help") && obj.contains("args"))
        {
          if (obj.contains("api"))
            api = obj["api"].as_string();
          rl.register_command(obj["name"].as_string(), obj["help"].as_string(), std::bind(&ws_client::handle_command, &ws,
                              obj["args"].get_array(), std::placeholders::_1, api, std::placeholders::_2));
        }
      } 
    }
    else
    {
      std::cout << "error reading config file: expected an array!\n";
      std::exit(-1);
    }
  }
  catch (fc::exception &e)
  {
    std::cout << e.what() << "\n";
    std::exit(-1);
  }
}

int main(int argc, const char *argv[])
{
  static const char *version = "v0.0.1";
  std::string url;
  std::string config;
  std::string user;
  std::string password;

  try
  {
    boost::program_options::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "shows this help")
        ("config,c", boost::program_options::value<std::string>(&config)->required(), "path to config file")
        ("server,s", boost::program_options::value<std::string>(&url)->required(), "server url")
        ("password,p", boost::program_options::value<std::string>(&password)->default_value(""), "api password")
        ("user,u", boost::program_options::value<std::string>(&user)->default_value(""), "api user")
        ("version,v", "prints version");

    boost::program_options::variables_map vm;
    boost::program_options::store(parse_command_line(argc, argv, desc), vm);

    if (vm.count("help"))
    {
      std::cout << desc << "\n";
      return 1;
    }
    if (vm.count("version"))
    {
      std::cout << version << "\n";
      return 0;
    }

    boost::program_options::notify(vm);
  }
  catch (const boost::program_options::error &ex)
  {
    std::cerr << ex.what() << '\n';
    return -1;
  }

  ws_client ws(url, user, password);
  cr::readline c("> ");
  configure_readline(config, ws, c);

  c.register_command("info", "Gives info", std::bind(&ws_client::info, &ws, std::placeholders::_1, std::placeholders::_2));

  c.execute_command("help");

  int retCode;
  do {
    retCode = c.read_line();
    // We can also change the prompt based on last return value:
    if ( retCode == ret::Ok )
      c.set_prompt("> ");
    else
      c.set_prompt(":( > ");

    if ( retCode == 1 )
      std::cout << "Received error code 1\n";
    else if ( retCode == 2 )
      std::cout << "Received error code 2\n";
  }
  while (retCode != ret::Quit);

  return 0;
}
