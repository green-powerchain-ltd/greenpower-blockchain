#include "readline.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <tuple>

#include <cstring>
#include <readline/readline.h>
#include <readline/history.h>

namespace readlinepp {
    namespace {

        readline* currentConsole         = nullptr;
        HISTORY_STATE* emptyHistory     = history_get_history_state();

    }  /* namespace  */

    struct readline::impl {
        using registered_commands = std::map<std::string, std::tuple<std::string, readline::command_function>>;

        std::string       greeting_;
        // These are hardcoded commands. They do not do anything and are catched manually in the execute_command function.
        registered_commands  commands_;
        HISTORY_STATE*      history_    = nullptr;

        explicit impl(std::string greeting)
            : greeting_(std::move(greeting)) {}
        ~impl() {
            free(history_);
        }

        impl(impl const&) = delete;
        impl(impl&&) = delete;
        impl& operator = (impl const&) = delete;
        impl& operator = (impl&&) = delete;
    };

    // Here we set default commands, they do nothing since we quit with them
    // Quitting behaviour is hardcoded in read_line()
    readline::readline(std::string const& greeting)
        : pimpl_{ new impl{ greeting } }
    {
        // Init readline basics
        rl_attempted_completion_function = &readline::get_command_completions;

        // These are default hardcoded commands.
        // Help command lists available commands.
        pimpl_->commands_["help"] = std::make_tuple("Prints help", [this](const std::string &, const arguments &){
            auto commands = get_registered_commands();
            std::cout << "Available commands are:\n";
            for ( auto & command : commands )
            {
                const auto &cmd = std::get<0>(command);
                std::cout << "\t" << cmd;
                std::string tab = "\t";
                if (cmd.size() < 8)
                    tab += "\t\t\t\t";
                else if (cmd.size() < 16)
                    tab += "\t\t\t";
                else if (cmd.size() < 24)
                    tab += "\t\t";
                else if (cmd.size() < 32)
                    tab += "\t";
                std::cout << tab << std::get<1>(command) << "\n";
            }
            return return_code::Ok;
        });
        // Run command executes all commands in an external file.
        pimpl_->commands_["run"] = std::make_tuple("", [this](const std::string &, const arguments & input) {
            if ( input.size() < 2 ) { std::cout << "Usage: " << input[0] << " script_filename\n"; return 1; }
            return execute_file(input[1]);
        });
        // Quit and Exit simply terminate the console.
        pimpl_->commands_["quit"] = std::make_tuple("Exits", [this](const std::string &, const arguments &) {
            return return_code::Quit;
        });

        pimpl_->commands_["exit"] = std::make_tuple("Exits", [this](const std::string &, const arguments &) {
            return return_code::Quit;
        });
    }

    readline::~readline() = default;

    void readline::register_command(const std::string &cmd, const std::string &help, command_function f) {
        pimpl_->commands_[cmd] = std::make_tuple(help, std::move(f));
    }

    std::vector<std::tuple<std::string, std::string>> readline::get_registered_commands() const {
        std::vector<std::tuple<std::string, std::string>> allCommands;
        for ( auto & pair : pimpl_->commands_ )
            allCommands.push_back(std::make_tuple(pair.first, std::get<0>(pair.second)));

        return allCommands;
    }

    void readline::save_state() {
        free(pimpl_->history_);
        pimpl_->history_ = history_get_history_state();
    }

    void readline::reserve_console() {
        if ( currentConsole == this ) return;

        // Save state of other readline
        if ( currentConsole )
            currentConsole->save_state();

        // Else we swap state
        if ( ! pimpl_->history_ )
            history_set_history_state(emptyHistory);
        else
            history_set_history_state(pimpl_->history_);

        // Tell others we are using the console
        currentConsole = this;
    }

    void readline::set_prompt(const std::string &greeting) {
        pimpl_->greeting_ = greeting;
    }

    std::string readline::get_prompt() const {
        return pimpl_->greeting_;
    }

    int readline::execute_command(const std::string &command) {
        // Convert input to vector
        std::vector<std::string> inputs;
        {
            std::istringstream iss(command);
            std::copy(std::istream_iterator<std::string>(iss),
                    std::istream_iterator<std::string>(),
                    std::back_inserter(inputs));
        }

        if ( inputs.empty() ) return return_code::Ok;

        impl::registered_commands::iterator it;
        if ( ( it = pimpl_->commands_.find(inputs[0]) ) != end(pimpl_->commands_) ) {
            return static_cast<int>((std::get<1>(it->second))(std::get<0>(it->second), inputs));
        }

        std::cout << "Command '" << inputs[0] << "' not found.\n";
        return return_code::Error;
    }

    int readline::execute_file(const std::string &filename) {
        std::ifstream input(filename);
        if ( ! input ) {
            std::cout << "Could not find the specified file to execute.\n";
            return return_code::Error;
        }
        std::string command;
        int counter = 0, result;

        while ( std::getline(input, command)  ) {
            if ( command[0] == '#' ) continue; // Ignore comments
            // Report what the readline is executing.
            std::cout << "[" << counter << "] " << command << '\n';
            if ( (result = execute_command(command)) ) return result;
            ++counter; std::cout << '\n';
        }

        // If we arrived successfully at the end, all is ok
        return return_code::Ok;
    }

    int readline::read_line() {
        reserve_console();

        char * buffer = ::readline(pimpl_->greeting_.c_str());
        if ( !buffer ) {
            std::cout << '\n'; // EOF doesn't put last endline so we put that so that it looks uniform.
            return return_code::Quit;
        }

        // TODO: Maybe add commands to history only if succeeded?
        if ( buffer[0] != '\0' )
            add_history(buffer);

        std::string line(buffer);
        free(buffer);

        return execute_command(line);
    }

    char **readline::get_command_completions(const char * text, int start, int) {
        char **completionList = nullptr;

        if ( start == 0 )
            completionList = rl_completion_matches(text, &readline::command_iterator);

        return completionList;
    }

    char *readline::command_iterator(const char * text, int state) {
        static impl::registered_commands::iterator it;
        if (!currentConsole)
            return nullptr;
        auto& commands = currentConsole->pimpl_->commands_;

        if ( state == 0 ) it = begin(commands);

        while ( it != end(commands) ) {
            auto & command = it->first;
            ++it;
            if ( command.find(text) != std::string::npos ) {
                return strdup(command.c_str());
            }
        }
        return nullptr;
    }
}
