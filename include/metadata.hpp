#ifndef TORRENT_METADATA_HPP
#define TORRENT_METADATA_HPP

#include <boost/url/urls.hpp>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "bencode_parser.hpp"

namespace torrent {

/*
 * A thread safe class to maintain metadata information of the torrent.
 * This info might come from a .torrent file or a magnet link.
 * Magnet links will give only a small part of this required metadata, 
 *      client should fetch it through peers later on. 
 * See metadata exchange extension: https://www.bittorrent.org/beps/bep_0009.html
 * */
class Metadata: public std::enable_shared_from_this<Metadata> {
  private:
    struct Private {
        explicit Private() = default;
    };

  public:
    Metadata(Private) {}

    static constexpr std::size_t BLOCK_LENGTH = 1 << 14;

  public:
    /*
     * Creates a Metadata object from the given .torrent file.
     * Will use a BencodeParser object to parse the file.
     * The Metadata object is ready to use after this function.
     * @param path A valid path to a .torrent file.
     * @throws std::runtime_exception If an error occurs while parsing the .torrent file.
     * */
    static std::shared_ptr<Metadata>
    from_torrent_file(const std::string_view path);

    /*
     * Creates a Metadata object from the given magnet link.
     * The Metadata object is not ready to use after this function.
     * load_info() must be called after obtaining the info directory from the peers.
     * */
    static std::shared_ptr<Metadata>
    from_magnet(const boost::url_view magnet_url);

    /*
     * Creates a Metadata object from the given parameter.
     * It will use a BencodeParser if its a .torrent file.
     * @param torrent Either should be a valid path to a .torrent file or a 
     *      magnet link. 
     * @throws std::runtime_exception If an error occurs while parsing the input.
     * */
    static std::shared_ptr<Metadata> create(const std::string_view torrent);

    /*
     * Loads the info directory to this Metadata object.
     * Can be called after constructing the object.
     * Function will set ready to true and call on_ready_callback.
     * @param info The info directory to fill the Metadata object.
     * @param info_hash The SHA1 hash of the given info directory.
     * */
    void load_info(BencodeParser::Element info, std::string info_hash);

    std::shared_ptr<Metadata> get_ptr() {
        return shared_from_this();
    }

    friend std::ostream& operator<<(std::ostream& os, const Metadata& metadata);

  public:
    /*
     * Returns the info hash by getting the SHA1 of the given 
     *    info directory in bencoded format.
     * */
    static std::string get_info_hash(const BencodeParser::Element& info);

  public:
    /* BEP9 Extension(See: https://www.bittorrent.org/beps/bep_0009.html) */

    /*
     * Returns true if the torrent is ready to download.
     * */
    bool is_ready() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return ready;
    }

    /*
     * Calls the give callback when all the information 
     *     is gathered to start downloading the torrent.
     * */
    void on_ready(std::function<void()> callback) {
        std::unique_lock<std::mutex> lock {mutex};
        if (ready) {
            lock.unlock();
            callback();
        } else {
            on_ready_callback = {callback};
        }
    }

    /*
     * Waits until the metadata is ready to use.
     * */
    void wait() {
        std::unique_lock<std::mutex> lock {ready_cv_mutex};
        ready_cv.wait(lock, [self = get_ptr()] { return self->ready; });
    }

    /*
     * Wakes all current waits.
     * */
    void stop() {
        std::scoped_lock<std::mutex> lock {ready_cv_mutex};
        ready = true;
        ready_cv.notify_all();
    }

  private:
    bool ready = false;
    std::optional<std::function<void()>> on_ready_callback;

    std::condition_variable ready_cv;
    std::mutex ready_cv_mutex;

  public:
    /* Getters */
    const std::string& get_info_hash() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return info_hash;
    }

    const auto& get_trackers() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return trackers;
    }

    const std::string& get_name() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return name;
    }

    const std::string& get_file_name() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return file_name;
    }

    std::size_t get_piece_length() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return piece_length;
    }

    std::size_t get_total_length() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return total_length;
    }

    /*
     * Returns a const reference to file information.
     * @return A vector of pair. First value is the length of the file, 
     *      second is the path value of the file.
     * */
    const auto& get_files() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return files;
    }

    const std::string& get_pieces() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return pieces;
    }

    std::size_t get_downloaded() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return downloaded;
    }

    std::size_t get_uploaded() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return uploaded;
    }

    std::size_t get_left() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return left;
    }

    std::size_t get_piece_count() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return pieces.size() / 20;
    }

    std::size_t get_pieces_done() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return pieces_done;
    }

    std::size_t get_block_count() const {
        std::scoped_lock<std::mutex> lock {mutex};
        return piece_length / BLOCK_LENGTH;
    }

    bool is_file_complete() const {
        std::scoped_lock<std::mutex> lock {mutex};
        auto piece_count =
            (total_length / piece_length) + (total_length % piece_length != 0);
        return piece_count == pieces_done;
    }

  public:
    /* Additional member functions */

    /*
     * Should be called once when a piece is completed.
     * Increases the pieces_done by 1.
     * Decreases left by the piece_length of that piece. 
     * @param 
     * */
    void on_piece_complete(std::size_t piece_index) {
        std::scoped_lock<std::mutex> lock {mutex};
        const auto piece_count = pieces.size() / 20;
        pieces_done += 1;
        if (piece_index == piece_count - 1) {
            // The last pieces can be a little bit shorter than usual pieces.
            left -= total_length - (piece_count - 1) * piece_length;
        } else {
            left -= piece_length;
        }
    }

    /*
     * Increases the member downloaded with the given amount.
     * */
    void increase_downloaded(std::size_t bytes_downloaded) {
        std::scoped_lock<std::mutex> lock {mutex};
        downloaded += bytes_downloaded;
    }

    /*
     * Function will increase the uploaded amount by the given parameter.
     * */
    void increase_uploaded(std::size_t bytes_uploaded) {
        std::scoped_lock<std::mutex> lock {mutex};
        uploaded += bytes_uploaded;
    }

  private:
    mutable std::mutex mutex;

    std::string info_hash;
    std::vector<std::string> trackers; // A list of tracker URIs;

    std::string name; // Name of the torrent.
    std::string
        file_name; // Name of the file we will write to while downloading.
    std::size_t piece_length = 0;
    std::size_t total_length = 0;
    std::vector<std::pair<std::size_t, std::string>> files;

    std::string pieces;

    std::size_t downloaded = 0;
    std::size_t uploaded = 0;
    std::size_t left = 0;

    std::size_t pieces_done = 0;
};

} // namespace torrent

#endif
