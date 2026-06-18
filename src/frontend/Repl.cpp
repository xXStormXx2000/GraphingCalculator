#include "Repl.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#else
#include <system_error>
#endif

#include "Grapher.h"

namespace calc {

	// Frontend lives in calc; pull backend names in from calc::core.
	using namespace core;

	namespace {

		// Directory containing the running executable. Data files are resolved
		// relative to this rather than the current working directory, so the
		// program finds them no matter where it is launched from. Each platform
		// branch asks the OS for the executable's own path; on any failure we
		// fall back to the current directory, which is no worse than before and
		// still non-fatal because every data lookup has a built-in fallback.
		std::filesystem::path executableDir() {
#if defined(_WIN32)
			std::wstring buf(MAX_PATH, L'\0');
			for (;;) {
				const DWORD len =
					GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
				if (len == 0) return std::filesystem::current_path();
				// Truncated: the buffer was too small. Grow and retry.
				if (len == buf.size()) { buf.resize(buf.size() * 2); continue; }
				buf.resize(len);
				break;
			}
			return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
			std::uint32_t size = 0;
			_NSGetExecutablePath(nullptr, &size);  // first call reports needed size
			std::string buf(size, '\0');
			if (_NSGetExecutablePath(buf.data(), &size) != 0)
				return std::filesystem::current_path();
			if (auto pos = buf.find('\0'); pos != std::string::npos) buf.resize(pos);
			std::error_code ec;
			std::filesystem::path canonical = std::filesystem::weakly_canonical(buf, ec);
			if (ec) return std::filesystem::path(buf).parent_path();
			return canonical.parent_path();
#else
			std::error_code ec;
			const std::filesystem::path self =
				std::filesystem::read_symlink("/proc/self/exe", ec);
			if (ec) return std::filesystem::current_path();
			return self.parent_path();
#endif
		}


		const std::unordered_map<std::string, std::string>& uiFallbacks() {
			static const std::unordered_map<std::string, std::string> table = {
																			   {"error_prefix",     "error:"},
																			   {"prompt_prefix",    ">>"},
																			   {"banner",
																				"#########################################\n"
																				"####                                 ####\n"
																				"####   Storm's graphing calculator   ####\n"
																				"####                                 ####\n"
																				"#########################################\n"
																				"\n"
																				"Operators: +  -  *  /  ^  :\n"
																				"Functions: sin cos tan asin acos atan sqrt abs log root\n"
																				"Constants: PI tau e phi G c h hbar k_B N_A R q_e\n"
																				"Commands:  /graph  /list  /clear  /help  /exit\n"
																				"Type '/help(topic)' for details.\n"},
																			   {"quitting",         "Quitting"},
																			   {"no_variables",     "(no variables defined)"},
																			   {"cleared",          "All variables cleared"},
																			   {"usage_help",       "usage: /help(topic)"},
																			   {"no_help",          "no help available for '{}'"},
																			   {"graph_arg_count",  "/graph expects 7 arguments: "
																								   "x_name, y_name, x_min, x_max, y_min, y_max, equation"},
																			   {"bound_not_number", "{} must be a number"},
																			   {"unknown_command",  "unknown command '/{}'"},
																			   {"bound_x_min",      "x_min"},
																			   {"bound_x_max",      "x_max"},
																			   {"bound_y_min",      "y_min"},
																			   {"bound_y_max",      "y_max"},
			};
			return table;
		}

		// Built-in fallback message used only when the error-strings file is
		// missing or has no entry for a code.
		std::string fallbackMessage(const Diagnostic& d) {
			return "error code " + std::to_string(static_cast<int>(d.code)) +
				(d.detail.empty() ? "" : " (" + d.detail + ")");
		}

		// Replace the first "{}" in `tmpl` with `detail`. If there is no
		// placeholder, returns the template unchanged.
		std::string fillTemplate(const std::string& tmpl, const std::string& detail) {
			const std::size_t pos = tmpl.find("{}");
			if (pos == std::string::npos) return tmpl;
			return tmpl.substr(0, pos) + detail + tmpl.substr(pos + 2);
		}

	}  // namespace

	std::string Repl::ui(const std::string& key) const {
		if (std::optional<std::string> v = m_ui.get(key)) return *v;
		const auto& fb = uiFallbacks();
		const auto it = fb.find(key);
		return it != fb.end() ? it->second : key;  // last resort: echo the key
	}

	std::string Repl::canonicalCommand(const std::string& typed) const {
		if (std::optional<std::string> id = m_commands.get(typed)) return *id;
		return typed;  // no mapping loaded: assume the typed name is canonical
	}

	std::string Repl::formatDiagnostic(const Diagnostic& d,
		const std::string& source) const {
		const std::string key = std::to_string(static_cast<int>(d.code));
		std::optional<std::string> tmpl = m_errors.get(key);
		const std::string message =
			tmpl ? fillTemplate(*tmpl, d.detail) : fallbackMessage(d);

		std::ostringstream oss;
		oss << ui("error_prefix") << ' ' << message;
		const bool spanInRange =
			d.span.end <= source.size() && d.span.begin <= d.span.end;
		if (spanInRange) {
			oss << "\n  " << source << "\n  ";
			for (std::size_t i = 0; i < d.span.begin; ++i) oss << ' ';
			const std::size_t len =
				d.span.end > d.span.begin ? d.span.end - d.span.begin : 1;
			for (std::size_t i = 0; i < len; ++i) oss << '^';
		}
		return oss.str();
	}

	std::optional<std::string> Repl::helpFor(const std::string& topic) const {
		return m_help.get(topic);
	}

	Repl::Repl(std::istream& in, std::ostream& out) : m_in(in), m_out(out) {
		// Presentation strings live in external data files so they can be
		// edited or localized without rebuilding. Resolve them relative to the
		// executable's own location, not the current working directory, so the
		// program works when launched from anywhere. Missing files are not
		// fatal: every lookup falls back to built-in English text.
		const std::filesystem::path base = executableDir();
		m_errors.loadFromFile((base / "Errors.txt").string());
		m_help.loadFromFile((base / "Help.txt").string());
		m_ui.loadFromFile((base / "Ui.txt").string());
		m_commands.loadFromFile((base / "Commands.txt").string());
	}

	void Repl::printBanner() {
		m_out << ui("banner") << '\n';
	}

	int Repl::run() {
		std::string line;
		while (m_running && std::getline(m_in, line)) {
			const std::string response = processLine(line);
			if (!response.empty()) {
				m_out << ui("prompt_prefix") << ' ' << response << '\n';
			}
		}
		return 0;
	}

	std::string Repl::processLine(const std::string& line) {
		// Trim leading whitespace to detect commands vs expressions.
		std::size_t i = 0;
		while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;

		if (i == line.size()) return "";  // empty / whitespace-only

		if (line[i] == '/') return handleCommand(line);
		return handleExpression(line);
	}

	std::string Repl::handleExpression(const std::string& line) {
		Result<CalculatorCore::EvalResult> r = m_core.evaluateLine(line, m_maxSize);
		if (!r) return formatDiagnostic(r.error(), line);
		const CalculatorCore::EvalResult& res = r.value();
		if (res.assignedName) return *res.assignedName + ": " + res.canonical;
		return res.canonical;
	}

	std::string Repl::handleCommand(const std::string& source) {
		Result<ParsedCommand> cmdResult = parseCommand(source);
		if (!cmdResult) return formatDiagnostic(cmdResult.error(), source);
		const ParsedCommand& cmd = cmdResult.value();

		// Resolve the (possibly localized) typed name to a canonical id.
		const std::string id = canonicalCommand(cmd.name);

		// Helper to format a command-level error: "<error_prefix> <message>".
		auto commandError = [&](const std::string& message) {
			return ui("error_prefix") + " " + message;
			};

		if (id == "exit") {
			m_running = false;
			return ui("quitting");
		}

		if (id == "list") {
			const auto names = m_core.definedNames();
			if (names.empty()) return ui("no_variables");
			std::ostringstream oss;
			oss << '\n';
			for (const std::string& name : names)
				oss << name << ": " << *m_core.definitionOf(name) << '\n';
			std::string s = oss.str();
			if (!s.empty() && s.back() == '\n') s.pop_back();
			return s;
		}

		if (id == "clear") {
			m_core.clear();
			return ui("cleared");
		}

		if (id == "help") {
			if (cmd.args.size() != 1) {
				return commandError(ui("usage_help"));
			}
			const auto& topic = cmd.args[0];

			// Prepend the numeric value if this topic evaluates to a number
			// (i.e. it's a built-in constant), then fall through to help text.
			std::string prefix;
			{
				auto r = m_core.evaluateLine(topic, m_maxSize);
				if (r && r.value().value) {
					prefix = topic + " = " + r.value().canonical + "\n  ";
				}
			}
			std::optional<std::string> text = helpFor(topic);
			if (!text) {
				if (!prefix.empty()) return prefix;  // known constant, no description
				return commandError(fillTemplate(ui("no_help"), topic));
			}
			return prefix + *text;
		}

		if (id == "graph") {
			if (cmd.args.size() != 7) {
				return commandError(ui("graph_arg_count"));
			}

			const std::string& xName = cmd.args[0];
			const std::string& yName = cmd.args[1];
			const std::string& eqName = cmd.args[6];

			// Parse numeric bounds by running them through the engine.
			static const char* boundKeys[] = {
											  "bound_x_min", "bound_x_max", "bound_y_min", "bound_y_max" };
			double bounds[4];
			for (std::size_t k = 0; k < 4; ++k) {
				Result<CalculatorCore::EvalResult> r = m_core.evaluateLine(cmd.args[k + 2], m_maxSize);
				if (!r || !r.value().value) {
					return commandError(
						fillTemplate(ui("bound_not_number"), ui(boundKeys[k])));
				}
				bounds[k] = *r.value().value;
			}

			// The ASCII renderer samples sparsely and decides each cell by sign
			// change, so it needs the asymptote-suppressed form: ask compilePlot
			// to clear denominators. (The default functor is raw L - R.)
			Result<PlotFunctor> functorR = m_core.compilePlot({
				.equationName = eqName,
				.axisNames = {xName, yName},
				.clearDenominators = true,
				});
			if (!functorR) return formatDiagnostic(functorR.error(), source);

			GraphRequest req{
				xName, yName,
				Rect{bounds[0], bounds[1], bounds[2], bounds[3]},
				std::move(functorR).value()
			};
			Result<std::string> result = renderGraph(req);
			if (!result) return formatDiagnostic(result.error(), source);
			return "\n" + std::move(result).value();
		}

		// Unknown command: report the name the user actually typed.
		return commandError(fillTemplate(ui("unknown_command"), cmd.name));
	}

}  // namespace calc