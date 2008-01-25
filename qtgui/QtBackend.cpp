/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "QtBackend.h"
#include <QKeyEvent>
#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <boost/format.hpp>
#include <iostream>

QtBackend::QtBackend() {
	viewport = 0;
	needsrender = false;

	for (unsigned int i = 0; i < 256; i++) {
		downkeys[i] = false;
	}
}

void QtBackend::init() {
}

void QtBackend::setup(QWidget *vp) {
	viewport = vp;

#if defined(Q_WS_X11)
	// on X11, using SDL_WINDOWID works, thank goodness.
	((QApplication *)QApplication::instance())->syncX();

	std::string windowidstr = boost::str(boost::format("SDL_WINDOWID=0x%lx") % viewport->winId());
	putenv((char *)windowidstr.c_str());
#else
	// alas, it sucks on Windows and OS X, so we use an offscreen buffer instead
	putenv("SDL_VIDEODRIVER=dummy");
#endif
	
	SDLBackend::init();

	/*
	char videodrivername[200];
	std::cout << "SDL video driver: " << SDL_VideoDriverName(videodrivername, 200) << std::endl;
	*/

	viewport->setCursor(Qt::BlankCursor);
}

int QtBackend::idealBpp() {
#if defined(Q_WS_X11)
	return SDLBackend::idealBpp();
#endif

	// TODO: handle 8bpp for C1 and 16bpp for Windows
	return 32;
}

void QtBackend::renderDone() {
	needsrender = false;

#if !defined(Q_WS_X11)
	// We need to copy the contents of the offscreen buffer into the window.
	// Note that we don't bother to lock because we know the dummy driver doesn't bother with locking.

	// As a generic method, we use Qt's code.	
	SDL_Surface *surf = getMainSDLSurface();
	QImage img((uchar *)surf->pixels, surf->w, surf->h, QImage::Format_RGB32);
	QPainter painter(viewport);
	painter.drawImage(0, 0, img);
#endif
}

void QtBackend::resized(int w, int h) {
	resizeNotify(w, h);

	// add resize window event to backend queue
	SomeEvent e;
	e.type = eventresizewindow;
	e.x = w;
	e.y = h;
	pushEvent(e);
}
	
bool QtBackend::pollEvent(SomeEvent &e) {
	// obtain events from backend
	if (SDLBackend::pollEvent(e)) return true;

	if (events.size() == 0)
		return false;

	e = events.front();
	events.pop_front();
	return true;
}

void QtBackend::pushEvent(SomeEvent e) {
	events.push_back(e);
}

// TODO: handle f keys (112-123 under windows, SDLK_F1 = 282 under sdl)
// see SDLBackend

int translateQtKey(int qtkey) {
	switch (qtkey) {
		case Qt::Key_Backspace: return 8;
		case Qt::Key_Tab: return 9;
		case Qt::Key_Clear: return 12;
		case Qt::Key_Return: return 13;
		case Qt::Key_Enter: return 13;
		case Qt::Key_Shift: return 16;
		case Qt::Key_Control: return 17;
		case Qt::Key_Pause: return 19;
		case Qt::Key_CapsLock: return 20;
		case Qt::Key_Escape: return 27;
		case Qt::Key_PageUp: return 33;
		case Qt::Key_PageDown: return 34;
		case Qt::Key_End: return 35;
		case Qt::Key_Home: return 36;
		case Qt::Key_Left: return 37;
		case Qt::Key_Up: return 38;
		case Qt::Key_Right: return 39;
		case Qt::Key_Down: return 40;
		case Qt::Key_Print: return 42;
		case Qt::Key_Insert: return 45;
		case Qt::Key_Delete: return 46;
		case Qt::Key_NumLock: return 144;
		default: return -1;
	}
}

void QtBackend::keyEvent(QKeyEvent *k, bool pressed) {
	int key = translateQtKey(k->key());
	if (key != -1) {
		SomeEvent e;
		if (pressed)
			e.type = eventspecialkeydown;
		else
			e.type = eventspecialkeyup;
		e.key = key;
		pushEvent(e);
		downkeys[key] = pressed;
		return;
	}

	for (int i = 0; i < k->text().size(); i++) {
		// TODO: openc2e probably doesn't like latin1
		char x = k->text().at(i).toLatin1();
		if (x != 0) {
			// We have a Latin-1 key which we can process.
			SomeEvent e;
			
			// the engine only handles eventkeydown at present
			if (pressed) {
				e.type = eventkeydown;
				e.key = x;
				pushEvent(e);
			}

			// letters/numbers get magic
			if (x >= 97 && x <= 122) { // lowercase letters
				e.key = x - 32; // capitalise
			} else if (x >= 48 && x <= 57) { // numbers
				e.key = x;
			} else continue;
			
			if (pressed)
				e.type = eventspecialkeydown;
			else
				e.type = eventspecialkeyup;

			pushEvent(e);
			downkeys[e.key] = pressed;
		}
	}
}

bool QtBackend::keyDown(int key) {
	return downkeys[key];
}
