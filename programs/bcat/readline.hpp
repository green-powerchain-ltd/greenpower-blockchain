#ifndef CONSOLE_CONSOLE_HEADER_FILE
#define CONSOLE_CONSOLE_HEADER_FILE

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace readlinepp {
    class readline {
        public:
            /**
             * @brief This is the function type that is used to interface with the Console class.
             *
             * These are the functions that are going to get called by Console
             * when the user types in a message. The vector will hold the
             * command elements, and the function needs to return its result.
             * The result can either be Quit (-1), OK (0), or an arbitrary
             * error (>=1).
             */
            using arguments = std::vector<std::string>;
            using command_function = std::function<int(const std::string &, const arguments &)>;

            enum return_code {
                Quit = -1,
                Ok = 0,
                Error = 1 // Or greater!
            };

            /**
             * @brief Basic constructor.
             *
             * The Console comes with two predefined commands: "quit" and
             * "exit", which both terminate the console, "help" which prints a
             * list of all registered commands, and "run" which executes script
             * files.
             *
             * These commands can be overridden or unregistered - but remember
             * to leave at least one to quit ;).
             *
             * @param greeting This represents the prompt of the Console.
             */
            explicit readline(std::string const& greeting);

            /**
             * @brief Basic destructor.
             *
             * Frees the history which is been produced by GNU readline.
             */
            ~readline();

            /**
             * @brief This function registers a new command within the Console.
             *
             * If the command already existed, it overwrites the previous entry.
             *
             * @param cmd The name of the command as inserted by the user.
             * @param help The help associated with the command.
             * @param f The function that will be called once the user writes the command.
             */
            void register_command(const std::string &cmd, const std::string &help, command_function f);

            /**
             * @brief This function returns a list with the currently available commands.
             *
             * @return A vector containing all registered commands names.
             */
            std::vector<std::tuple<std::string, std::string>> get_registered_commands() const;

            /**
             * @brief Sets the prompt for this Console.
             *
             * @param greeting The new greeting.
             */
            void set_prompt(const std::string &greeting);

            /**
             * @brief Gets the current prompt of this Console.
             *
             * @return The currently set greeting.
             */
            std::string get_prompt() const;

            /**
             * @brief This function executes an arbitrary string as if it was inserted via stdin.
             *
             * @param command The command that needs to be executed.
             *
             * @return The result of the operation.
             */
            int execute_command(const std::string &command);

            /**
             * @brief This function calls an external script and executes all commands inside.
             *
             * This function stops execution as soon as any single command returns something
             * different from 0, be it a quit code or an error code.
             *
             * @param filename The pathname of the script.
             *
             * @return What the last command executed returned.
             */
            int execute_file(const std::string &filename);

            /**
             * @brief This function executes a single command from the user via stdin.
             *
             * @return The result of the operation.
             */
            int read_line();

            readline(const readline&) = delete;
            readline(readline&&) = delete;
            readline& operator = (readline const&) = delete;
            readline& operator = (readline&&) = delete;

        private:
            struct impl;
            using pimpl = std::unique_ptr<impl>;
            pimpl pimpl_;

            /**
             * @brief This function saves the current state so that some other Console can make use of the GNU readline facilities.
             */
            void save_state();
            /**
             * @brief This function reserves the use of the GNU readline facilities to the calling Console instance.
             */
            void reserve_console();

            // GNU newline interface to our commands.
            using command_completer_function = char**(const char * text, int start, int end);
            using command_iterator_function = char*(const char * text, int state);

            static command_completer_function get_command_completions;
            static command_iterator_function command_iterator;
    };
}

#endif
