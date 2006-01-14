/*
 *  World.cpp
 *  openc2e
 *
 *  Created by Alyssa Milburn on Tue May 25 2004.
 *  Copyright (c) 2004 Alyssa Milburn. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 */

#include "World.h"
#include "caosVM.h" // for setupCommandPointers()
#include "PointerAgent.h"
#include "CompoundAgent.h" // for setFocus
#include <limits.h> // for MAXINT
#include "creaturesImage.h"

#include <boost/filesystem/convenience.hpp>
namespace fs = boost::filesystem;

World world;

World::World() {
	ticktime = 50;
	tickcount = 0;
	quitting = saving = false;
}

// annoyingly, if we put this in the constructor, imageGallery isn't available yet
void World::init() {
	// TODO: pick up hand image name from "Pointer Information" catalogue tag
	if (gallery.getImage("hand")) // TODO: we refcount one too many here, i expect // TODO: we refcount one too many here, i expect
		theHand = new PointerAgent("hand");
	else	
		theHand = new PointerAgent("syst"); // Creatures Village
}

caosVM *World::getVM(Agent *a) {
	if (vmpool.empty()) {
		return new caosVM(a);
	} else {
		caosVM *x = vmpool.back();
		vmpool.pop_back();
		x->setOwner(a);
		return x;
	}
}

void World::freeVM(caosVM *v) {
	v->setOwner(0);
	vmpool.push_back(v);
}

void World::queueScript(unsigned short event, AgentRef agent, AgentRef from, caosVar p0, caosVar p1) {
	scriptevent e;

	assert(agent);

	e.scriptno = event;
	e.agent = agent;
	e.from = from;
	e.p[0] = p0;
	e.p[1] = p1;

	scriptqueue.push_back(e);
}

// TODO: eventually, the part should be referenced via a weak_ptr, maaaaybe?
void World::setFocus(TextEntryPart *p) {
	// Unfocus the current agent. Not sure if c2e always does this (what if the agent/part is bad?).
        if (focusagent) {
		CompoundAgent *c = dynamic_cast<CompoundAgent *>(focusagent.get());
		assert(c);
		TextEntryPart *p = dynamic_cast<TextEntryPart *>(c->part(focuspart));
		if (p)
			p->loseFocus();
	}

	if (!p)
		focusagent.clear();
	else {
		p->gainFocus();
		focusagent = p->getParent();
		focuspart = p->id;
	}
}

void World::tick() {
	if (saving) {} // TODO: save
	if (quitting) { exit(0); }
	// Tick all agents.
	for (std::list<Agent *>::iterator i = agents.begin(); i != agents.end(); i++) {
		(**i).tick();
	}
	
	// Process the script queue.
	for (std::list<scriptevent>::iterator i = scriptqueue.begin(); i != scriptqueue.end(); i++) {
		if (i->agent && i->agent->fireScript(i->scriptno, i->from)) {
			assert(i->agent->vm);
			i->agent->vm->setVariables(i->p[0], i->p[1]);
			i->agent->vmTick();
		}
	}
	scriptqueue.clear();
	
	// Do the actual killing of agent objects.
	// TODO: should we do this before+after the script queue processing? - fuzzie
	while (killqueue.size()) {
		Agent *rip = killqueue.back();
		killqueue.pop_back();
		assert(rip->dying);
		delete rip;
	}


	tickcount++;
	// todo: tick rooms
}

Agent *World::agentAt(unsigned int x, unsigned int y, bool obey_all_transparency, bool needs_mouseable) {
	CompoundPart *p = partAt(x, y, obey_all_transparency, needs_mouseable);
	if (p)
		return p->getParent();
	else
		return 0;
}

CompoundPart *World::partAt(unsigned int x, unsigned int y, bool obey_all_transparency, bool needs_mouseable) {
	for (std::multiset<CompoundPart *, partzorder>::iterator i = zorder.begin(); i != zorder.end(); i++) {
		int ax = (int)x - (*i)->getParent()->x;
		int ay = (int)y - (*i)->getParent()->y;
		if ((*i)->x <= ax) if ((*i)->y <= ay) if (((*i) -> x + (int)(*i)->getWidth()) >= ax) if (((*i) -> y + (int)(*i)->getHeight()) >= ay)
			if ((*i)->getParent() != theHand) {
				if ((*i)->is_transparent)
					if (obey_all_transparency || (*i)->id == 0)
						if ((*i)->transparentAt(ax - (*i)->x, ay - (*i)->y))
							continue;
			if (needs_mouseable && !((*i)->getParent()->mouseable))
				continue;
			return *i;
		}
	}
	
	return 0;
}

int World::getUNID(Agent *whofor) {
	do {
		int unid = rand();
		if (!unidmap[unid]) {
			unidmap[unid] = whofor;
			return unid;
		}
	} while (1);
}

void World::freeUNID(int unid) {
	unidmap.erase(unid);
}

Agent *World::lookupUNID(int unid) {
	return unidmap[unid];
}

void World::drawWorld() {
	MetaRoom *m = camera.getMetaRoom();
	if (!m) {
		// Whoops - the room we're in vanished, or maybe we were never in one?
		// Try to get a new one ...
		m = map.getFallbackMetaroom();
		if (!m) {
			std::cerr << "ERROR: No metarooms! Panicing ..." << std::endl;
			abort();
		}
		camera.goToMetaRoom(m->id);
	}
	int adjustx = camera.getX();
	int adjusty = camera.getY();
	blkImage *test = m->backImage();

	// draw the blk
	for (unsigned int i = 0; i < (test->totalheight / 128); i++) {
		for (unsigned int j = 0; j < (test->totalwidth / 128); j++) {
			// figure out which block number to use
			unsigned int whereweare = j * (test->totalheight / 128) + i;
			
			SDL_Rect destrect;
			destrect.x = (j * 128) - adjustx + m->x();
			destrect.y = (i * 128) - adjusty + m->y();

			// if the block's on screen, blit it.
			if ((destrect.x >= -128) && (destrect.y >= -128) &&
					(destrect.x - 128 <= backend.getWidth()) &&
					(destrect.y - 128 <= backend.getHeight()))
				SDL_BlitSurface(backsurfs[m->id][whereweare], 0, backend.screen, &destrect);
		}
	}

	// render all the agents
	for (std::multiset<renderable *, renderablezorder>::iterator i = renders.begin(); i != renders.end(); i++) {
		(*i)->render(&backend, -adjustx, -adjusty);
	}

	if (showrooms) {
		Room *r = map.roomAt(hand()->x, hand()->y);
		for (std::vector<Room *>::iterator i = camera.getMetaRoom()->rooms.begin();
				 i != camera.getMetaRoom()->rooms.end(); i++) {
			unsigned int col = 0xFFFF00CC;
			if (*i == r) col = 0xFF00FFCC;
			else if (r) {
				if ((**i).doors[r])
					col = 0x00FFFFCC;
			}
			// ceiling
			backend.renderLine(
					(**i).x_left - adjustx,
					(**i).y_left_ceiling - adjusty,
					(**i).x_right - adjustx,
					(**i).y_right_ceiling - adjusty,
					col);
			// floor
			backend.renderLine(
					(**i).x_left - adjustx, 
					(**i).y_left_floor - adjusty,
					(**i).x_right - adjustx,
					(**i).y_right_floor - adjusty,
					col);
			// left side
			backend.renderLine(
					(**i).x_left - adjustx,
					(**i).y_left_ceiling - adjusty,
					(**i).x_left - adjustx,
					(**i).y_left_floor - adjusty,
					col);
			// right side
			backend.renderLine(
					(**i).x_right  - adjustx,
					(**i).y_right_ceiling - adjusty,
					(**i).x_right - adjustx,
					(**i).y_right_floor - adjusty,
					col);
		}
	}

	SDL_Flip(backend.screen);
}

void World::executeInitScript(fs::path p) {
	assert(fs::exists(p));
	assert(!fs::is_directory(p));

	std::string x = p.native_file_string();
	std::ifstream s(x.c_str());
	assert(s.is_open());
	std::cout << "executing script " << x << "...\n";
	std::cout.flush(); std::cerr.flush();
	try {
		caosScript script(gametype, x);
		script.parse(s);
		caosVM vm(0);
		script.installScripts();
		vm.runEntirely(script.installer);
	} catch (std::exception &e) {
		std::cerr << "script exec failed due to exception " << e.what() << std::endl;
	}
	std::cout.flush(); std::cerr.flush();
}

void World::executeBootstrap(fs::path p) {
	if (!fs::is_directory(p)) {
		executeInitScript(p);
		return;
	}

	std::vector<fs::path> scripts;
	
	fs::directory_iterator fsend;
	for (fs::directory_iterator d(p); d != fsend; ++d) {
		if ((!fs::is_directory(*d)) && (fs::extension(*d) == ".cos"))
			scripts.push_back(*d);
	}

	std::sort(scripts.begin(), scripts.end());
	for (std::vector<fs::path>::iterator i = scripts.begin(); i != scripts.end(); i++)
		executeInitScript(*i);
}

void World::executeBootstrap(bool switcher) {
	// TODO: this code is possibly wrong with multiple bootstrap directories
	std::multimap<unsigned int, fs::path> bootstraps;

	std::cout << "executing all bootstraps" << std::endl;

	for (std::vector<fs::path>::iterator i = data_directories.begin(); i != data_directories.end(); i++) {
		assert(fs::exists(*i));
		assert(fs::is_directory(*i));
		std::cout << "executing bootstraps in " << i->native_directory_string() << std::endl;
		if (fs::is_directory(*i / "/Bootstrap/")) {
			std::cout << "found bootstrap dir" << std::endl;
			fs::directory_iterator fsend;
			// iterate through each bootstrap directory
			for (fs::directory_iterator d(*i / "/Bootstrap/"); d != fsend; ++d) {
				if (fs::exists(*d) && fs::is_directory(*d)) {
					std::string s = (*d).leaf();
					std::cout << "wondering about " << s << std::endl; 
					if (s.size() > 3) {
						char x[4]; x[0] = s[0]; x[1] = s[1]; x[2] = s[2]; x[3] = 0;
						if (!(isdigit(x[0]) && isdigit(x[1]) && isdigit(x[2]))) continue; // TODO: correct?
						unsigned int z = atoi(x);
						if (switcher && z != 0) continue;
						if (!switcher && z == 0) continue;

						bootstraps.insert(std::pair<unsigned int, fs::path>(z, *d));
					}
				}
			}
		}
	}

	for (std::multimap<unsigned int, fs::path>::iterator i = bootstraps.begin(); i != bootstraps.end(); i++) {
		executeBootstrap(i->second);
	}
}

/* vim: set noet: */
