/*
   Copyright (C) 2003 - 2016 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#include "global.hpp"
#include "gettext.hpp"
#include "log.hpp"

#include <fstream>
#include <locale>
#include <boost/locale.hpp>
// including boost/thread fixes linking of boost locale for msvc on boost 1.60
#include <boost/thread.hpp>
#include <boost/filesystem/path.hpp>
#include <map>
#include "spirit_po.hpp"

#define DBG_G LOG_STREAM(debug, lg::general())
#define LOG_G LOG_STREAM(info, lg::general())
#define WRN_G LOG_STREAM(warn, lg::general())
#define ERR_G LOG_STREAM(err, lg::general())

using po_catalog = spirit_po::catalog<>;
namespace bl = boost::locale;
namespace fs = boost::filesystem;

namespace
{
	class utf8_locale_name
	{
	public:
		utf8_locale_name()
			: name_()
		{
			LOG_G << "Generating default locale\n";
			try
			{
				//NOTE: the default_locale object needs to live as least as long as the locale_info object. Otherwise the program will segfault.
				std::locale default_locale = bl::generator().generate("");
				const bl::info& locale_info = std::use_facet<bl::info>(default_locale);
				name_ += locale_info.language();
				if(!locale_info.country().empty())
					country_ = locale_info.country();
				if(!locale_info.variant().empty())
					variant_ = locale_info.variant();
			}
			catch(const std::exception& e)
			{
				ERR_G << "Failed to generate default locale string. message:" << e.what() << std::endl;
			}
			LOG_G << "Finished generating default locale, default is now '" << name_ << "'\n";
		}

		utf8_locale_name(const std::string& language)
		{
			std::string::size_type at_pos = language.rfind('@');
			std::string::size_type line_pos = language.find('_');
			name_ = language.substr(0, line_pos);
			if(line_pos != std::string::npos)
				country_ = language.substr(line_pos + 1, at_pos - line_pos);
			if(at_pos != std::string::npos)
				variant_ = language.substr(at_pos + 1);
		}

		std::string full_name()
		{
			std::string name = name_;
			if(!country_.empty())
				name += "_" + country_;
			if(!variant_.empty())
				name += "@" + variant_;
			return name;
		}

		std::string short_name()
		{
			return name_;
		}

		std::locale get_locale(const bl::generator& gen)
		{
			std::string name = full_name();
			if(variant_.empty()) {
				name += ".UTF-8";
			} else {
				int at = name.find_first_of('@');
				name.insert(at, ".UTF-8");
			}
			return gen.generate(name);
		}

	private:
		std::string name_, country_, variant_;
	};
	struct translation_manager
	{
		translation_manager()
			: loaded_domains_()
			, current_language_()
			, generator_()
			, current_locale_()
			, is_dirty_(true)
		{
			generator_.use_ansi_encoding(false);
			generator_.categories(bl::information_facet | bl::collation_facet);
			generator_.characters(bl::char_facet);
			//we cannot have current_locale_ be a non boost generated locale since it might not supply
			//the boost::locale::info facet. as soon as we add message paths update_locale_internal might fail
			//for example because of invalid .po files. So make sure we call it at least once before adding paths/domains
			update_locale_internal();
		}

		void load_catalog(const std::string& domain, const fs::path& base_path)
		{
			std::ifstream po_file;
			fs::path po_path = base_path/domain;
			po_path /= current_language_.full_name() + ".po";
			po_file.open(po_path.c_str());
			if(!po_file.good()) {
				po_file.clear(); // Clear error flags
				po_path = base_path/domain;
				po_path /= current_language_.short_name() + ".po";
				po_file.open(po_path.c_str());
			}
			if(po_file.good()) {
				DBG_G << "Successfully loaded language file from " << po_path << "\n";
				msg_catalog.insert({domain, po_catalog::from_istream(po_file)});
			} else if(current_language_.full_name() != "c") {
				WRN_G << "Error opening language file for " << current_language_.full_name() << ", textdomain " << domain << "\n";
				WRN_G << "  (Most likely this means the file does not exist or lacks read permission.)\n";
			}
		}

		void add_messages_domain(const std::string& domain, fs::path path)
		{
			if(loaded_domains_.find(domain) != loaded_domains_.end())
			{
				return;
			}

			if(loaded_domains_[domain] == path)
			{
				return;
			}

			// Load the catalog file if it exists
			load_catalog(domain, path);
			loaded_domains_[domain] = path;
		}

		void set_language(const std::string& language)
		{
			if(language.empty()) {
				current_language_ = utf8_locale_name();
			} else {
				current_language_ = utf8_locale_name(language);
			}
			// Reload all the catalog files for the new language setting
			msg_catalog.clear();
			for(auto p : loaded_domains_) {
				load_catalog(p.first, p.second);
			}
			update_locale();
		}

		void update_locale()
		{
			is_dirty_ = true;
		}

		void update_locale_internal()
		{
			try
			{
				LOG_G << "attempting to generate locale by name '" << current_language_.full_name() << "'\n";
				current_locale_ = current_language_.get_locale(generator_);
				const bl::info& info = std::use_facet< bl::info >(current_locale_);
				LOG_G << "updated locale to '" << current_language_.full_name() << "' locale is now '" << current_locale_.name() << "' ( "
				      << "name='" << info.name()
				      << "' country='"  << info.country()
				      << "' language='"  << info.language()
				      << "' encoding='"  << info.encoding()
				      << "' variant='"  << info.variant() << "')\n";
			}
			catch(const bl::conv::conversion_error&)
			{
				assert(std::has_facet<bl::info>(current_locale_));
				const bl::info& info = std::use_facet< bl::info >(current_locale_);
				ERR_G << "Failed to update locale due to conversion error, locale is now: "
				      << "name='" << info.name()
				      << "' country='" << info.country()
				      << "' language='" << info.language()
				      << "' encoding='" << info.encoding()
				      << "' variant='" << info.variant()
				      << "'" << std::endl;
			}
			is_dirty_ = false;
		}

		const std::locale& get_locale()
		{
			if(is_dirty_)
			{
				update_locale_internal();
			}
			return current_locale_;
		}

		const char* gettext(const char* domain, const char* msgid)
		{
			auto iter = msg_catalog.find(domain);
			if(iter == msg_catalog.end()) {
				return msgid;
			}
			return iter->second.gettext(msgid);
		}

		const char* ngettext(const char* domain, const char* singular, const char* plural, int n)
		{
			auto iter = msg_catalog.find(domain);
			if(iter == msg_catalog.end()) {
				return n == 1 ? plural : singular;
			}
			return iter->second.ngettext(singular, plural, n);
		}

	private:
		std::map<std::string, fs::path> loaded_domains_;
		std::map<std::string, po_catalog> msg_catalog;
		utf8_locale_name current_language_;
		bl::generator generator_;
		std::locale current_locale_;
		bool is_dirty_;
	};

	translation_manager& get_manager()
	{
		static translation_manager* mng = new translation_manager();
		return *mng;
	}

}

namespace translation
{

std::string dgettext(const char* domain, const char* msgid)
{
	return get_manager().gettext(domain, msgid);
}

std::string dsgettext (const char * domainname, const char *msgid)
{
	const char* msgval = get_manager().gettext(domainname, msgid);
	if (msgval == msgid) {
		const char* firsthat = std::strrchr (msgid, '^');
		if (firsthat == nullptr)
			msgval = msgid;
		else
			msgval = firsthat + 1;
	}
	return msgval;
}

std::string dsngettext (const char * domainname, const char *singular, const char *plural, int n)
{
	const char* msgval = get_manager().ngettext(domainname, singular, plural, n);
	if (msgval == singular) {
		const char* firsthat = std::strrchr(singular, '^');
		if (firsthat == nullptr)
			msgval = singular;
		else
			msgval = firsthat + 1;
	}
	return msgval;
}

void bind_textdomain(const char* domain, const char* directory, const char* /*encoding*/)
{
	LOG_G << "adding textdomain '" << domain << "' in directory '" << directory << "'\n";
	get_manager().add_messages_domain(domain, directory);
	get_manager().update_locale();
}

void set_default_textdomain(const char* domain)
{
	LOG_G << "set_default_textdomain: '" << domain << "'\n";
//	get_manager().set_default_messages_domain(domain);
}

void set_language(const std::string& language, const std::vector<std::string>* /*alternates*/)
{
	// why should we need alternates? which languages we support should only be related
	// to which languages we ship with and not which the os supports
	LOG_G << "setting language to  '" << language << "' \n";
	get_manager().set_language(language);
}

int compare(const std::string& s1, const std::string& s2)
{
	return std::use_facet<std::collate<char> >(get_manager().get_locale()).compare(s1.c_str(), s1.c_str() + s1.size(), s2.c_str(), s2.c_str() + s2.size());
}

void init()
{
	LOG_G << "initializing languages\n";
}
}
