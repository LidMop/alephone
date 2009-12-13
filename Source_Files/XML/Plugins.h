/*
 *  Plugins.h - a plugin manager

	Copyright (C) 2009 and beyond by Gregory Smith
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html

*/

#ifndef PLUGINS_H
#define PLUGINS_H

#include "FileHandler.h"
#include <string>
#include <vector>

struct Plugin {
	DirectorySpecifier directory;
	std::string name;
	std::string description;
	std::string version;
	std::vector<std::string> mmls;
	std::string hud_lua;
	std::string solo_lua;
	std::string required_version;

	bool enabled;
	bool compatible() const;
};

class Plugins {
	friend class XML_PluginParser;
public:
	static Plugins* instance();
	typedef std::vector<Plugin>::iterator iterator;
	
	void enumerate();
	void load_mml();
	void load_solo_mml();

	void disable(const std::string& path);

	iterator begin() { return m_plugins.begin(); }
	iterator end() { return m_plugins.end(); }

	const Plugin* find_hud_lua() const;
	const Plugin* find_solo_lua() const;
private:
	Plugins() { }
	void add(Plugin plugin) { m_plugins.push_back(plugin); }

	static Plugins* m_instance;
	std::vector<Plugin> m_plugins;
};


#endif
