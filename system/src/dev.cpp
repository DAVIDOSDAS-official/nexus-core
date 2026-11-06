#include <nexus/system/dev.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace nexus::system {

const std::vector<DevLanguage>& devLanguages() {
    static const std::vector<DevLanguage> langs = {
        {"cpp", "C and C++", {"cpp", "c++", "c", "cxx", "gcc"},
         {"gcc", "gcc-c++", "make", "cmake", "ninja-build", "gdb"},
         {"gcc", "g++", "make", "cmake", "ninja", "gdb"}, "g++ --version", true},
        {"python", "Python", {"python", "python3", "py"},
         {"python3", "python3-pip", "python3-virtualenv"},
         {"python3", "pip3"}, "python3 --version", true},
        {"js", "JavaScript (Node.js)", {"js", "javascript", "node", "nodejs", "npm", "typescript", "ts"},
         {"nodejs", "nodejs-npm"},
         {"node", "npm", "npx"}, "node --version", false},
        {"java", "Java", {"java", "jdk", "openjdk", "maven"},
         {"java-devel", "maven"},
         {"java", "javac", "jar", "mvn"}, "java -version", false},
        {"rust", "Rust", {"rust", "rustc", "cargo", "rs"},
         {"rust", "cargo", "rustfmt", "clippy"},
         {"rustc", "cargo", "rustfmt", "cargo-clippy"}, "rustc --version", false},
        {"go", "Go", {"go", "golang"},
         {"golang"},
         {"go", "gofmt"}, "go version", false},
        {"ruby", "Ruby", {"ruby", "rb", "gem", "rails"},
         {"ruby", "ruby-devel", "rubygems", "rubygem-bundler"},
         {"ruby", "gem", "bundle", "irb"}, "ruby --version", false},
    };
    return langs;
}

std::optional<DevLanguage> findDevLanguage(const std::string& typed) {
    std::string t;
    for (char c : typed) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    for (const auto& l : devLanguages()) {
        if (std::find(l.names.begin(), l.names.end(), t) != l.names.end()) return l;
    }
    return std::nullopt;
}

std::string devImageFor(const std::string& osRelease) {
    std::istringstream in(osRelease);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("VERSION_ID=", 0) != 0) continue;
        std::string v = line.substr(11);
        v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
        v.erase(std::remove(v.begin(), v.end(), '\''), v.end());
        const bool digits = !v.empty() &&
            std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        return std::string("registry.fedoraproject.org/fedora-toolbox:") + (digits ? v : "latest");
    }
    return "registry.fedoraproject.org/fedora-toolbox:latest";
}

namespace {
const std::map<std::string, std::string>& topics() {
    static const std::map<std::string, std::string> t = {
        {"git",
         "git clone <url>                  copy a project to this machine\n"
         "git status                       what changed\n"
         "git add -A && git commit -m \"..\" save a snapshot of everything\n"
         "git push / git pull              send / fetch snapshots\n"
         "git log --oneline -10            the last ten snapshots\n"
         "git diff                         the changes not saved yet\n"
         "git switch -c <name>             a new branch, and move to it\n"
         "git restore <file>               throw away changes to one file\n"
         "git stash / git stash pop        put changes aside / bring them back\n"},
        {"python",
         "python3 -m venv .venv            a private package folder for this project\n"
         ". .venv/bin/activate             use it (deactivate to stop)\n"
         "pip install <package>            install into it, never into the system\n"
         "pip freeze > requirements.txt    write down what the project needs\n"
         "pip install -r requirements.txt  install exactly that elsewhere\n"
         "python3 -m http.server 8000      serve this folder at http://localhost:8000\n"
         "python3 -i script.py             run, then stay in the shell to poke at it\n"},
        {"js",
         "npm init -y                      start a project (package.json)\n"
         "npm install <package>            add a dependency\n"
         "npm install -D <package>         add one only needed while developing\n"
         "npm run <script>                 run a script from package.json\n"
         "npx <tool>                       run a tool without installing it\n"
         "npm outdated / npm update        what is old / update within the rules\n"
         "node script.js                   run a file\n"},
        {"java",
         "javac Main.java && java Main     compile and run one file\n"
         "java Main.java                   or run it straight from source\n"
         "mvn archetype:generate           start a Maven project\n"
         "mvn package                      build it (the .jar lands in target/)\n"
         "java -jar target/app.jar         run the result\n"},
        {"rust",
         "cargo new <name>                 start a project\n"
         "cargo run                        build and run\n"
         "cargo build --release            optimised build (target/release/)\n"
         "cargo add <crate>                add a dependency\n"
         "cargo test                       run the tests\n"
         "cargo fmt / cargo clippy         format / lint\n"},
        {"go",
         "go mod init <module>             start a module\n"
         "go run .                         build and run\n"
         "go build                         a single binary, no dependencies\n"
         "go get <module>                  add a dependency\n"
         "go test ./...                    run every test\n"
         "gofmt -w .                       format everything\n"},
        {"ruby",
         "ruby script.rb                   run a file\n"
         "irb                              an interactive Ruby shell\n"
         "gem install <gem>                install a gem (into your home folder)\n"
         "bundle init / bundle add <gem>   a Gemfile, and add to it\n"
         "bundle install / bundle exec ... install the Gemfile / run with it\n"},
        {"cpp",
         "g++ -std=c++20 -Wall main.cpp -o app && ./app   one file\n"
         "cmake -S . -B build              configure a CMake project\n"
         "cmake --build build -j$(nproc)   build it on every core\n"
         "ctest --test-dir build           run its tests\n"
         "gdb ./app                        debug (run, bt, break main, next)\n"
         "-fsanitize=address,undefined     add to catch memory bugs while testing\n"},
        {"containers",
         "podman run --rm -it fedora bash  a throwaway Fedora, gone when you exit\n"
         "podman ps -a / podman images     containers / images here\n"
         "podman build -t name .           build an image from a Containerfile\n"
         "podman run -p 8080:80 name       publish a container's port 80 as 8080\n"
         "distrobox create / enter         a long-lived box that shares your home\n"
         "(podman understands the docker commands; alias docker=podman)\n"},
        {"ports",
         "ss -tlnp                         what is listening, and which program\n"
         "ss -tnp                          open connections\n"
         "sudo firewall-cmd --list-all     what the firewall lets in\n"
         "sudo firewall-cmd --add-port=8080/tcp            open one until restart\n"
         "sudo firewall-cmd --add-port=8080/tcp --permanent and keep it\n"
         "curl -I http://localhost:8080    does it answer?\n"},
        {"ssh",
         "ssh-keygen -t ed25519            make a key pair (once)\n"
         "ssh-copy-id user@host            let that key in on another machine\n"
         "ssh user@host                    log in\n"
         "scp file user@host:/path         copy a file over\n"
         "ssh -L 8080:localhost:80 user@host   reach its port 80 at your 8080\n"},
    };
    return t;
}
}  // namespace

std::string devHelp(const std::string& topic) {
    std::string key = topic;
    if (auto lang = findDevLanguage(topic)) key = lang->key;
    if (key == "docker" || key == "podman" || key == "container") key = "containers";
    if (key == "port" || key == "network") key = "ports";
    const auto it = topics().find(key);
    return it == topics().end() ? "" : it->second;
}

std::vector<std::string> devHelpTopics() {
    std::vector<std::string> out;
    for (const auto& [k, v] : topics()) out.push_back(k);
    return out;
}

}  // namespace nexus::system
