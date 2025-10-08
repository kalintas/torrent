### A Torrent Client written in C++
A simple CLI torrent client. Currently supports both torrent files and magnet links. 

Supports UDP and HTTP/HTTPS for trackers and TCP for the peers.

Isn't very efficient at the moment meaning it take a long time to finish the download.

## Installing the pre commit hooks
Repository uses pre commit hooks that do auto clang format. To install the pre commit hooks:
```
brew install pre-commit
pre-commit install
```

## Resources
https://wiki.theory.org/BitTorrentSpecification
https://www.bittorrent.org/beps/bep_0000.html

TODO |
-----
Distributed Hash Table
Auto seeding after finishing the downloading
More CLI options 
Does not work with every torrent file
Testing
