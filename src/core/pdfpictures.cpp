#include "pdfpictures.h"

#include <QFile>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTransform>
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>

namespace {

// ===================================================================== objects

struct Obj;
using ObjArray = QVector<Obj>;
using ObjDict = QMap<QByteArray, Obj>;

struct Obj {
    enum Type { Null, Bool, Number, String, Name, Array, Dict, Ref, Keyword };
    Type type = Null;
    double num = 0;
    bool boolean = false;
    QByteArray bytes;   // string contents, name without '/', keyword
    std::shared_ptr<ObjArray> array;
    std::shared_ptr<ObjDict> dict;
    int ref = 0;
    // An indirect object followed by stream data: where the data lies.
    qint64 streamStart = -1;
    qint64 streamLength = -1;

    bool isName(const char *n) const { return type == Name && bytes == n; }
    bool isKeyword(const char *k) const { return type == Keyword && bytes == k; }
    bool isStream() const { return type == Dict && streamStart >= 0; }
    const Obj &operator[](const char *key) const;
    int size() const { return type == Array && array ? array->size() : 0; }
    const Obj &at(int i) const;
    int toInt(int fallback = 0) const { return type == Number ? int(std::lround(num)) : fallback; }
    double toReal(double fallback = 0) const { return type == Number ? num : fallback; }
};

const Obj &nullObj() {
    static const Obj null;
    return null;
}

const Obj &Obj::operator[](const char *key) const {
    if (type != Dict || !dict) return nullObj();
    auto it = dict->constFind(QByteArray(key));
    return it == dict->constEnd() ? nullObj() : it.value();
}

const Obj &Obj::at(int i) const {
    if (type != Array || !array || i < 0 || i >= array->size()) return nullObj();
    return array->at(i);
}

// ======================================================================= lexer

inline bool isWhite(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
inline bool isDelim(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}'
        || c == '/' || c == '%';
}
inline bool isRegular(char c) { return !isWhite(c) && !isDelim(c); }
inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Lexer {
public:
    Lexer(const char *data, qint64 size, qint64 pos = 0) : s(data), n(size), pos(pos) {}
    const char *s;
    qint64 n;
    qint64 pos;

    bool atEnd() { skipSpace(); return pos >= n; }

    void skipSpace() {
        while (pos < n) {
            const char c = s[pos];
            if (isWhite(c)) { ++pos; continue; }
            if (c == '%') {
                while (pos < n && s[pos] != '\n' && s[pos] != '\r') ++pos;
                continue;
            }
            break;
        }
    }

    bool startsWith(const char *word) const {
        const qint64 len = qint64(std::strlen(word));
        return pos + len <= n && std::memcmp(s + pos, word, size_t(len)) == 0;
    }

    Obj parse(int depth = 0) {
        Obj o;
        skipSpace();
        if (pos >= n || depth > 64) return o;
        const char c = s[pos];
        if (c == '/') {
            ++pos;
            o.type = Obj::Name;
            while (pos < n && isRegular(s[pos])) {
                if (s[pos] == '#' && pos + 2 < n && hexValue(s[pos + 1]) >= 0 && hexValue(s[pos + 2]) >= 0) {
                    o.bytes.append(char(hexValue(s[pos + 1]) * 16 + hexValue(s[pos + 2])));
                    pos += 3;
                } else {
                    o.bytes.append(s[pos++]);
                }
            }
            return o;
        }
        if (c == '(') return literalString();
        if (c == '<') {
            if (pos + 1 < n && s[pos + 1] == '<') {
                pos += 2;
                o.type = Obj::Dict;
                o.dict = std::make_shared<ObjDict>();
                while (true) {
                    skipSpace();
                    if (pos >= n) break;
                    if (s[pos] == '>' && pos + 1 < n && s[pos + 1] == '>') { pos += 2; break; }
                    const qint64 before = pos;
                    Obj key = parse(depth + 1);
                    if (key.type != Obj::Name) {
                        if (pos == before) ++pos;   // never stall on junk
                        continue;
                    }
                    skipSpace();
                    if (pos < n && s[pos] == '>' && pos + 1 < n && s[pos + 1] == '>') {
                        (*o.dict)[key.bytes] = Obj();
                        continue;
                    }
                    (*o.dict)[key.bytes] = parse(depth + 1);
                }
                return o;
            }
            ++pos;
            o.type = Obj::String;
            int high = -1;
            while (pos < n && s[pos] != '>') {
                const int v = hexValue(s[pos++]);
                if (v < 0) continue;
                if (high < 0) high = v;
                else { o.bytes.append(char(high * 16 + v)); high = -1; }
            }
            if (high >= 0) o.bytes.append(char(high * 16));
            if (pos < n) ++pos;
            return o;
        }
        if (c == '[') {
            ++pos;
            o.type = Obj::Array;
            o.array = std::make_shared<ObjArray>();
            while (true) {
                skipSpace();
                if (pos >= n) break;
                if (s[pos] == ']') { ++pos; break; }
                const qint64 before = pos;
                o.array->append(parse(depth + 1));
                if (pos == before) ++pos;
            }
            return o;
        }
        if (c == ']' || c == '>' || c == ')' || c == '{' || c == '}') {
            ++pos;
            o.type = Obj::Keyword;
            o.bytes = QByteArray(1, c);
            return o;
        }
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            o = number();
            // "12 0 R" is a reference.
            if (o.type == Obj::Number && o.num == std::floor(o.num) && o.num >= 0) {
                const qint64 save = pos;
                skipSpace();
                if (pos < n && s[pos] >= '0' && s[pos] <= '9') {
                    Obj gen = number();
                    skipSpace();
                    if (gen.type == Obj::Number && pos < n && s[pos] == 'R'
                        && (pos + 1 >= n || !isRegular(s[pos + 1]))) {
                        ++pos;
                        Obj r;
                        r.type = Obj::Ref;
                        r.ref = int(o.num);
                        return r;
                    }
                }
                pos = save;
            }
            return o;
        }
        while (pos < n && isRegular(s[pos])) o.bytes.append(s[pos++]);
        if (o.bytes == "true" || o.bytes == "false") {
            o.type = Obj::Bool;
            o.boolean = o.bytes == "true";
        } else if (o.bytes == "null") {
            o.type = Obj::Null;
        } else {
            o.type = Obj::Keyword;
        }
        return o;
    }

private:
    Obj number() {
        Obj o;
        const qint64 start = pos;
        while (pos < n && (std::isdigit(uchar(s[pos])) || s[pos] == '.' || s[pos] == '-' || s[pos] == '+')) ++pos;
        bool ok = false;
        const double v = QByteArray(s + start, int(pos - start)).toDouble(&ok);
        o.type = Obj::Number;
        o.num = ok ? v : 0.0;
        return o;
    }

    Obj literalString() {
        Obj o;
        o.type = Obj::String;
        ++pos;
        int depth = 1;
        while (pos < n) {
            char c = s[pos++];
            if (c == '\\' && pos < n) {
                c = s[pos++];
                switch (c) {
                case 'n': o.bytes.append('\n'); break;
                case 'r': o.bytes.append('\r'); break;
                case 't': o.bytes.append('\t'); break;
                case 'b': o.bytes.append('\b'); break;
                case 'f': o.bytes.append('\f'); break;
                case '\r': if (pos < n && s[pos] == '\n') ++pos; break;   // line continuation
                case '\n': break;
                default:
                    if (c >= '0' && c <= '7') {
                        int v = c - '0';
                        for (int k = 0; k < 2 && pos < n && s[pos] >= '0' && s[pos] <= '7'; ++k) v = v * 8 + (s[pos++] - '0');
                        o.bytes.append(char(v & 0xff));
                    } else {
                        o.bytes.append(c);
                    }
                }
                continue;
            }
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) break;
            o.bytes.append(c);
        }
        return o;
    }
};

// ===================================================================== filters

QByteArray inflateData(const QByteArray &in, bool *ok) {
    QByteArray out;
    *ok = false;
    for (int attempt = 0; attempt < 2 && !*ok; ++attempt) {
        out.clear();
        z_stream z{};
        // First as zlib data, then as raw deflate (some writers leave out the header).
        if (inflateInit2(&z, attempt == 0 ? 15 : -15) != Z_OK) return out;
        z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
        z.avail_in = uInt(in.size());
        char buffer[65536];
        int result = Z_OK;
        while (result == Z_OK) {
            z.next_out = reinterpret_cast<Bytef *>(buffer);
            z.avail_out = sizeof(buffer);
            result = inflate(&z, Z_NO_FLUSH);
            out.append(buffer, int(sizeof(buffer) - z.avail_out));
            if (result == Z_BUF_ERROR && z.avail_in == 0) break;   // truncated: keep what came out
        }
        inflateEnd(&z);
        *ok = result == Z_STREAM_END || (!out.isEmpty() && (result == Z_BUF_ERROR || result == Z_DATA_ERROR));
    }
    return out;
}

QByteArray lzwDecode(const QByteArray &in, int earlyChange) {
    QByteArray out;
    std::vector<QByteArray> table;
    auto reset = [&] {
        table.clear();
        for (int i = 0; i < 256; ++i) table.emplace_back(1, char(i));
        table.emplace_back();   // 256 clear
        table.emplace_back();   // 257 end
    };
    reset();
    int codeLength = 9;
    quint32 bitBuffer = 0;
    int bits = 0;
    QByteArray previous;
    const uchar *p = reinterpret_cast<const uchar *>(in.constData());
    for (int i = 0; i < in.size(); ++i) {
        bitBuffer = (bitBuffer << 8) | p[i];
        bits += 8;
        while (bits >= codeLength) {
            const int code = int((bitBuffer >> (bits - codeLength)) & ((1u << codeLength) - 1));
            bits -= codeLength;
            if (code == 256) {
                reset();
                codeLength = 9;
                previous.clear();
                continue;
            }
            if (code == 257) return out;
            QByteArray entry;
            if (code < int(table.size()) && (code < 256 || !table[size_t(code)].isEmpty())) {
                entry = table[size_t(code)];
            } else if (code == int(table.size()) && !previous.isEmpty()) {
                entry = previous + previous.at(0);
            } else {
                return out;   // damaged
            }
            out.append(entry);
            if (!previous.isEmpty() && table.size() < 4096) table.push_back(previous + entry.at(0));
            previous = entry;
            const int next = int(table.size()) + earlyChange;
            codeLength = next >= 2048 ? 12 : next >= 1024 ? 11 : next >= 512 ? 10 : 9;
        }
    }
    return out;
}

QByteArray asciiHexDecode(const QByteArray &in) {
    QByteArray out;
    int high = -1;
    for (char c : in) {
        if (c == '>') break;
        const int v = hexValue(c);
        if (v < 0) continue;
        if (high < 0) high = v;
        else { out.append(char(high * 16 + v)); high = -1; }
    }
    if (high >= 0) out.append(char(high * 16));
    return out;
}

QByteArray ascii85Decode(const QByteArray &in) {
    QByteArray out;
    quint32 tuple = 0;
    int count = 0;
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c == '~') break;
        if (isWhite(c)) continue;
        if (c == 'z' && count == 0) { out.append(4, '\0'); continue; }
        if (c < '!' || c > 'u') continue;
        tuple = tuple * 85 + quint32(c - '!');
        if (++count == 5) {
            for (int k = 3; k >= 0; --k) out.append(char((tuple >> (k * 8)) & 0xff));
            tuple = 0;
            count = 0;
        }
    }
    if (count > 1) {
        for (int k = count; k < 5; ++k) tuple = tuple * 85 + 84;
        for (int k = 0; k < count - 1; ++k) out.append(char((tuple >> ((3 - k) * 8)) & 0xff));
    }
    return out;
}

QByteArray runLengthDecode(const QByteArray &in) {
    QByteArray out;
    for (int i = 0; i < in.size();) {
        const int len = uchar(in.at(i++));
        if (len == 128) break;
        if (len < 128) {
            out.append(in.mid(i, len + 1));
            i += len + 1;
        } else if (i < in.size()) {
            out.append(257 - len, in.at(i++));
        }
    }
    return out;
}

QByteArray unpredict(const QByteArray &in, const Obj &parms) {
    const int predictor = parms["Predictor"].toInt(1);
    if (predictor < 2) return in;
    const int colors = std::max(1, parms["Colors"].toInt(1));
    const int bpc = std::max(1, parms["BitsPerComponent"].toInt(8));
    const int columns = std::max(1, parms["Columns"].toInt(1));
    const int bpp = std::max(1, colors * bpc / 8);
    const qint64 rowBytes = (qint64(colors) * bpc * columns + 7) / 8;
    if (rowBytes <= 0 || rowBytes > (1 << 26)) return in;
    QByteArray out;
    if (predictor == 2) {   // TIFF, 8-bit samples
        out = in;
        if (bpc != 8) return out;
        uchar *d = reinterpret_cast<uchar *>(out.data());
        for (qint64 row = 0; (row + 1) * rowBytes <= out.size(); ++row)
            for (qint64 i = bpp; i < rowBytes; ++i) d[row * rowBytes + i] += d[row * rowBytes + i - bpp];
        return out;
    }
    // PNG: each row starts with its filter type.
    const uchar *src = reinterpret_cast<const uchar *>(in.constData());
    const qint64 rows = in.size() / (rowBytes + 1);
    out.resize(int(rows * rowBytes));
    uchar *dst = reinterpret_cast<uchar *>(out.data());
    std::vector<uchar> zero(size_t(rowBytes), 0);
    for (qint64 r = 0; r < rows; ++r) {
        const int type = src[r * (rowBytes + 1)];
        const uchar *line = src + r * (rowBytes + 1) + 1;
        uchar *cur = dst + r * rowBytes;
        const uchar *prev = r > 0 ? dst + (r - 1) * rowBytes : zero.data();
        for (qint64 i = 0; i < rowBytes; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0;
            const int b = prev[i];
            const int c = i >= bpp ? prev[i - bpp] : 0;
            int v = line[i];
            switch (type) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: {
                const int p = a + b - c;
                const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: break;
            }
            cur[i] = uchar(v);
        }
    }
    return out;
}

QString compressionName(const QByteArray &filter) {
    if (filter == "DCTDecode" || filter == "DCT") return QStringLiteral("JPEG");
    if (filter == "JPXDecode") return QStringLiteral("JPEG 2000");
    if (filter == "JBIG2Decode") return QStringLiteral("JBIG2");
    if (filter == "CCITTFaxDecode" || filter == "CCF") return QStringLiteral("CCITT fax");
    if (filter == "FlateDecode" || filter == "Fl") return QStringLiteral("Flate");
    if (filter == "LZWDecode" || filter == "LZW") return QStringLiteral("LZW");
    if (filter == "RunLengthDecode" || filter == "RL") return QStringLiteral("RunLength");
    if (filter == "ASCII85Decode" || filter == "A85") return QStringLiteral("ASCII85");
    if (filter == "ASCIIHexDecode" || filter == "AHx") return QStringLiteral("ASCIIHex");
    return QString::fromLatin1(filter);
}

// Whether JPEG data has Photoshop's "Adobe" marker (APP14), which says how
// its CMYK values are stored.
[[maybe_unused]] bool hasAdobeMarker(const QByteArray &jpeg) {
    const int limit = int(std::min<qint64>(jpeg.size(), 65536)) - 9;
    for (int i = 2; i < limit; ++i)
        if (uchar(jpeg[i]) == 0xff && uchar(jpeg[i + 1]) == 0xee && jpeg.mid(i + 4, 5) == "Adobe") return true;
    return false;
}

bool isImageCodec(const QByteArray &f) {
    return f == "DCTDecode" || f == "DCT" || f == "JPXDecode" || f == "JBIG2Decode"
        || f == "CCITTFaxDecode" || f == "CCF";
}

// ================================================================ color spaces

struct ColorSpace {
    enum Kind { Gray, RGB, CMYK, Indexed, Separation, Lab, Unsupported };
    Kind kind = Gray;
    int components = 1;
    std::shared_ptr<ColorSpace> base;   // Indexed
    int hival = 0;
    QByteArray lookup;
    double labRange[4] = {-100, 100, -100, 100};
    QString name;
};

inline uchar clampByte(double v) { return uchar(std::clamp(std::lround(v), 0L, 255L)); }

QRgb labToRgb(double L, double a, double b) {
    // CIE L*a*b* (D50) to sRGB, close enough for a picture.
    auto f = [](double t) { return t > 6.0 / 29 ? t * t * t : 3 * (6.0 / 29) * (6.0 / 29) * (t - 4.0 / 29); };
    const double fy = (L + 16) / 116, fx = fy + a / 500, fz = fy - b / 200;
    const double X = 0.9642 * f(fx), Y = f(fy), Z = 0.8249 * f(fz);
    double r = 3.1339 * X - 1.6169 * Y - 0.4906 * Z;
    double g = -0.9785 * X + 1.9160 * Y + 0.0334 * Z;
    double bl = 0.0720 * X - 0.2290 * Y + 1.4057 * Z;
    auto gamma = [](double c) {
        c = std::clamp(c, 0.0, 1.0);
        return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
    };
    return qRgb(clampByte(gamma(r) * 255), clampByte(gamma(g) * 255), clampByte(gamma(bl) * 255));
}

// `v`: components 0..1 (Indexed: the index itself).
QRgb toRgb(const ColorSpace &cs, const double *v) {
    switch (cs.kind) {
    case ColorSpace::Gray: { const uchar g = clampByte(v[0] * 255); return qRgb(g, g, g); }
    case ColorSpace::RGB: return qRgb(clampByte(v[0] * 255), clampByte(v[1] * 255), clampByte(v[2] * 255));
    case ColorSpace::CMYK: {
        const double k = 1 - v[3];
        return qRgb(clampByte(255 * (1 - v[0]) * k), clampByte(255 * (1 - v[1]) * k), clampByte(255 * (1 - v[2]) * k));
    }
    case ColorSpace::Separation: { const uchar g = clampByte(255 * (1 - v[0])); return qRgb(g, g, g); }
    case ColorSpace::Lab:
        return labToRgb(v[0] * 100, cs.labRange[0] + v[1] * (cs.labRange[1] - cs.labRange[0]),
                        cs.labRange[2] + v[2] * (cs.labRange[3] - cs.labRange[2]));
    case ColorSpace::Indexed: {
        if (!cs.base) return qRgb(0, 0, 0);
        const int index = std::clamp(int(std::lround(v[0])), 0, cs.hival);
        const int n = cs.base->components;
        double comps[4] = {0, 0, 0, 0};
        for (int c = 0; c < n && c < 4; ++c) {
            const int at = index * n + c;
            comps[c] = at < cs.lookup.size() ? uchar(cs.lookup.at(at)) / 255.0 : 0.0;
        }
        return toRgb(*cs.base, comps);
    }
    case ColorSpace::Unsupported: break;
    }
    return qRgb(0, 0, 0);
}

}  // namespace

// ======================================================================== impl

struct PdfPictures::Impl {
    QByteArray data;
    struct Entry { Obj obj; qint64 position = -1; };
    QHash<int, Entry> objects;
    QVector<Obj> trailers;
    QVector<Picture> pictures;
    QString error;

    const Obj &resolve(const Obj &o, int depth = 0) const {
        if (o.type != Obj::Ref || depth > 32) return o;
        auto it = objects.constFind(o.ref);
        if (it == objects.constEnd()) return nullObj();
        return resolve(it->obj, depth + 1);
    }
    const Obj &get(const Obj &dict, const char *key) const { return resolve(dict[key]); }

    void record(int number, const Obj &obj, qint64 position) {
        auto it = objects.find(number);
        if (it == objects.end() || it->position <= position) objects.insert(number, Entry{obj, position});
    }

    // ---- reading the file ----
    void scan();
    void expandObjectStreams();
    bool streamBytes(const Obj &stream, QByteArray *out, QByteArray *codec, QString *problem) const;
    QByteArray contentBytes(const Obj &stream) const {
        QByteArray out, codec;
        QString problem;
        streamBytes(stream, &out, &codec, &problem);
        return codec.isEmpty() ? out : QByteArray();
    }
    ColorSpace colorSpace(const Obj &o, int depth = 0) const;
    void findPictures();
    void walkPages(const Obj &node, const Obj &resources, int depth, QSet<int> &visited, int *page);
    void walkContent(const QByteArray &content, const Obj &resources, QTransform ctm, int page, int depth,
                     QSet<int> &forms, QSet<int> &seen);
    void addPicture(int object, const Obj &image, const QTransform &ctm, int page, QSet<int> &seen);
    QString precheck(const Obj &image, QString *compression) const;
    QImage decodeImage(const Obj &image, QString *problem, int depth = 0) const;
    QImage decodeGrayMask(const Obj &mask, const QSize &size, bool stencil, QString *problem) const;
};

void PdfPictures::Impl::scan() {
    const char *s = data.constData();
    const qint64 n = data.size();
    qint64 pos = 0;
    while (pos < n) {
        const qint64 at = data.indexOf("obj", pos);
        if (at < 0) break;
        pos = at + 3;
        if (at + 3 < n && isRegular(s[at + 3])) continue;   // "objects", "obj2" ...
        // Look back for "<number> <generation> ".
        qint64 q = at - 1;
        if (q < 0 || !isWhite(s[q])) continue;
        while (q >= 0 && isWhite(s[q])) --q;
        const qint64 genEnd = q;
        while (q >= 0 && std::isdigit(uchar(s[q]))) --q;
        if (q == genEnd || q < 0 || !isWhite(s[q])) continue;
        while (q >= 0 && isWhite(s[q])) --q;
        const qint64 numEnd = q;
        while (q >= 0 && std::isdigit(uchar(s[q]))) --q;
        if (q == numEnd || (q >= 0 && isRegular(s[q]))) continue;
        bool ok = false;
        const int number = QByteArray(s + q + 1, int(numEnd - q)).toInt(&ok);
        if (!ok || number <= 0) continue;

        Lexer lx(s, n, at + 3);
        Obj obj = lx.parse();
        lx.skipSpace();
        if (obj.type == Obj::Dict && lx.startsWith("stream")) {
            qint64 start = lx.pos + 6;
            if (start < n && s[start] == '\r') ++start;
            if (start < n && s[start] == '\n') ++start;
            qint64 length = -1;
            const Obj &len = obj["Length"];
            if (len.type == Obj::Number && len.num >= 0 && start + qint64(len.num) <= n) {
                // Trust a direct /Length when "endstream" follows it.
                qint64 e = start + qint64(len.num);
                while (e < n && isWhite(s[e])) ++e;
                if (e + 9 <= n && std::memcmp(s + e, "endstream", 9) == 0) length = qint64(len.num);
            }
            if (length < 0) {
                const qint64 e = data.indexOf("endstream", start);
                qint64 end = e < 0 ? n : e;
                if (end > start && s[end - 1] == '\n') --end;
                if (end > start && s[end - 1] == '\r') --end;
                length = end - start;
            }
            obj.streamStart = start;
            obj.streamLength = length;
            const qint64 after = data.indexOf("endstream", start + length);
            pos = after < 0 ? n : after + 9;
        } else {
            pos = std::max(pos, lx.pos);
        }
        record(number, obj, at);
    }
    // Classic trailers ("trailer << ... >>").
    for (qint64 at = data.indexOf("trailer"); at >= 0; at = data.indexOf("trailer", at + 7)) {
        Lexer lx(s, n, at + 7);
        Obj t = lx.parse();
        if (t.type == Obj::Dict) trailers.append(t);
    }
}

void PdfPictures::Impl::expandObjectStreams() {
    const QList<int> numbers = objects.keys();
    for (int number : numbers) {
        const Entry container = objects.value(number);
        const Obj &o = container.obj;
        if (!o.isStream() || !o["Type"].isName("ObjStm")) continue;
        QByteArray bytes, codec;
        QString problem;
        if (!streamBytes(o, &bytes, &codec, &problem) || !codec.isEmpty()) continue;
        const int count = std::clamp(o["N"].toInt(0), 0, 1000000);
        const qint64 first = o["First"].toInt(0);
        Lexer header(bytes.constData(), bytes.size());
        for (int i = 0; i < count; ++i) {
            const Obj num = header.parse();
            const Obj offset = header.parse();
            if (num.type != Obj::Number || offset.type != Obj::Number) break;
            const qint64 at = first + offset.toInt();
            if (at < 0 || at >= bytes.size()) continue;
            Lexer body(bytes.constData(), bytes.size(), at);
            Obj inner = body.parse();
            // Newer than an earlier direct definition, older than a later one.
            record(num.toInt(), inner, container.position);
        }
    }
}

bool PdfPictures::Impl::streamBytes(const Obj &stream, QByteArray *out, QByteArray *codec, QString *problem) const {
    if (!stream.isStream()) return false;
    QByteArray bytes = data.mid(int(stream.streamStart), int(stream.streamLength));
    const Obj &filterObj = get(stream, "Filter");
    const Obj &parmsObj = get(stream, "DecodeParms").type != Obj::Null ? get(stream, "DecodeParms") : get(stream, "DP");
    QVector<QByteArray> filters;
    QVector<Obj> parms;
    if (filterObj.type == Obj::Name) {
        filters << filterObj.bytes;
        parms << parmsObj;
    } else if (filterObj.type == Obj::Array) {
        for (int i = 0; i < filterObj.size(); ++i) {
            filters << resolve(filterObj.at(i)).bytes;
            parms << (parmsObj.type == Obj::Array ? resolve(parmsObj.at(i)) : Obj());
        }
    }
    for (int i = 0; i < filters.size(); ++i) {
        const QByteArray &f = filters[i];
        const Obj &p = parms[i];
        if (isImageCodec(f)) {
            if (i != filters.size() - 1) {
                *problem = QStringLiteral("unusual compression");
                return false;
            }
            *codec = f;
            break;
        }
        if (f == "FlateDecode" || f == "Fl") {
            bool ok = false;
            bytes = inflateData(bytes, &ok);
            if (!ok) { *problem = QStringLiteral("damaged compressed data"); return false; }
            bytes = unpredict(bytes, p);
        } else if (f == "LZWDecode" || f == "LZW") {
            bytes = unpredict(lzwDecode(bytes, p["EarlyChange"].toInt(1)), p);
        } else if (f == "ASCIIHexDecode" || f == "AHx") {
            bytes = asciiHexDecode(bytes);
        } else if (f == "ASCII85Decode" || f == "A85") {
            bytes = ascii85Decode(bytes);
        } else if (f == "RunLengthDecode" || f == "RL") {
            bytes = runLengthDecode(bytes);
        } else if (f == "Crypt") {
            if (!p["Name"].isName("Identity") && p.type != Obj::Null) {
                *problem = QStringLiteral("encrypted data");
                return false;
            }
        } else {
            *problem = QStringLiteral("unknown compression %1").arg(QString::fromLatin1(f));
            return false;
        }
    }
    *out = bytes;
    return true;
}

ColorSpace PdfPictures::Impl::colorSpace(const Obj &in, int depth) const {
    ColorSpace cs;
    const Obj &o = resolve(in);
    if (depth > 8) { cs.kind = ColorSpace::Unsupported; return cs; }
    auto device = [&](const QByteArray &name) {
        if (name == "DeviceGray" || name == "G" || name == "CalGray") { cs.kind = ColorSpace::Gray; cs.components = 1; return true; }
        if (name == "DeviceRGB" || name == "RGB" || name == "CalRGB") { cs.kind = ColorSpace::RGB; cs.components = 3; return true; }
        if (name == "DeviceCMYK" || name == "CMYK") { cs.kind = ColorSpace::CMYK; cs.components = 4; return true; }
        return false;
    };
    if (o.type == Obj::Name) {
        if (!device(o.bytes)) { cs.kind = ColorSpace::Unsupported; cs.name = QString::fromLatin1(o.bytes); }
        return cs;
    }
    if (o.type != Obj::Array || o.size() == 0) {
        cs.kind = ColorSpace::Gray;   // no color space: assume gray
        return cs;
    }
    const QByteArray family = resolve(o.at(0)).bytes;
    if (device(family)) return cs;
    if (family == "ICCBased") {
        const Obj &profile = resolve(o.at(1));
        const int n = profile["N"].toInt(3);
        if (n == 1) { cs.kind = ColorSpace::Gray; cs.components = 1; }
        else if (n == 4) { cs.kind = ColorSpace::CMYK; cs.components = 4; }
        else if (n == 3) { cs.kind = ColorSpace::RGB; cs.components = 3; }
        else if (profile["Alternate"].type != Obj::Null) return colorSpace(profile["Alternate"], depth + 1);
        else cs.kind = ColorSpace::Unsupported;
        return cs;
    }
    if (family == "Indexed" || family == "I") {
        cs.kind = ColorSpace::Indexed;
        cs.components = 1;
        cs.base = std::make_shared<ColorSpace>(colorSpace(o.at(1), depth + 1));
        cs.hival = std::clamp(resolve(o.at(2)).toInt(255), 0, 255);
        const Obj &lookup = resolve(o.at(3));
        if (lookup.type == Obj::String) cs.lookup = lookup.bytes;
        else if (lookup.isStream()) cs.lookup = contentBytes(lookup);
        if (cs.base->kind == ColorSpace::Unsupported || cs.base->kind == ColorSpace::Indexed)
            cs.kind = ColorSpace::Unsupported;
        return cs;
    }
    if (family == "Separation") {
        cs.kind = ColorSpace::Separation;
        cs.components = 1;
        return cs;
    }
    if (family == "DeviceN") {
        const Obj &names = resolve(o.at(1));
        cs.components = std::max(1, names.size());
        cs.kind = cs.components == 1 ? ColorSpace::Separation : ColorSpace::Unsupported;
        cs.name = QStringLiteral("DeviceN");
        return cs;
    }
    if (family == "Lab") {
        cs.kind = ColorSpace::Lab;
        cs.components = 3;
        const Obj &range = resolve(o.at(1))["Range"];
        for (int i = 0; i < 4 && i < range.size(); ++i) cs.labRange[i] = resolve(range.at(i)).toReal(cs.labRange[i]);
        return cs;
    }
    cs.kind = ColorSpace::Unsupported;
    cs.name = QString::fromLatin1(family);
    return cs;
}

QString PdfPictures::Impl::precheck(const Obj &image, QString *compression) const {
    const Obj &filterObj = get(image, "Filter");
    QByteArray last;
    if (filterObj.type == Obj::Name) last = filterObj.bytes;
    else if (filterObj.type == Obj::Array && filterObj.size() > 0) last = resolve(filterObj.at(filterObj.size() - 1)).bytes;
    *compression = last.isEmpty() ? QStringLiteral("uncompressed") : compressionName(last);
    if (last == "JPXDecode")
        return QStringLiteral("This picture is stored as JPEG 2000, which Paint.with.me cannot read.");
    if (last == "JBIG2Decode")
        return QStringLiteral("This picture is stored as JBIG2 (a format for scanned black and white pages), "
                              "which Paint.with.me cannot read.");
    if (last == "CCITTFaxDecode" || last == "CCF")
        return QStringLiteral("This picture is stored as CCITT fax data, which Paint.with.me cannot read.");
    if (!get(image, "ImageMask").boolean && last != "DCTDecode" && last != "DCT") {
        const ColorSpace cs = colorSpace(image["ColorSpace"]);
        if (cs.kind == ColorSpace::Unsupported)
            return QStringLiteral("This picture uses a color space Paint.with.me cannot show (%1).")
                .arg(cs.name.isEmpty() ? QStringLiteral("unknown") : cs.name);
    }
    return QString();
}

void PdfPictures::Impl::addPicture(int object, const Obj &image, const QTransform &ctm, int page, QSet<int> &seen) {
    if (seen.contains(object)) return;
    seen.insert(object);
    Picture p;
    p.page = page;
    p.object = object;
    p.size = QSize(get(image, "Width").toInt(0), get(image, "Height").toInt(0));
    if (p.size.width() < 4 || p.size.height() < 4) return;
    // The unit square of the picture lands on the page through the CTM.
    const double wide = std::hypot(ctm.m11(), ctm.m12());
    const double high = std::hypot(ctm.m21(), ctm.m22());
    if (wide > 0.01 && high > 0.01 && !ctm.isIdentity()) {
        p.dpiX = p.size.width() * 72.0 / wide;
        p.dpiY = p.size.height() * 72.0 / high;
    }
    p.problem = precheck(image, &p.compression);
    pictures.append(p);
}

void PdfPictures::Impl::walkContent(const QByteArray &content, const Obj &resources, QTransform ctm, int page,
                                    int depth, QSet<int> &forms, QSet<int> &seen) {
    Lexer lx(content.constData(), content.size());
    QVector<Obj> operands;
    QVector<QTransform> stack;
    while (!lx.atEnd()) {
        const qint64 before = lx.pos;
        Obj o = lx.parse();
        if (lx.pos == before) ++lx.pos;
        if (o.type != Obj::Keyword) {
            if (operands.size() < 32) operands.append(o);
            continue;
        }
        const QByteArray &op = o.bytes;
        if (op == "q") {
            stack.append(ctm);
        } else if (op == "Q") {
            if (!stack.isEmpty()) ctm = stack.takeLast();
        } else if (op == "cm" && operands.size() >= 6) {
            const int k = operands.size() - 6;
            const QTransform m(operands[k].num, operands[k + 1].num, operands[k + 2].num, operands[k + 3].num,
                               operands[k + 4].num, operands[k + 5].num);
            ctm = m * ctm;
        } else if (op == "Do" && !operands.isEmpty() && operands.last().type == Obj::Name) {
            const Obj &xobjects = get(resolve(resources), "XObject");
            const Obj &entry = xobjects[operands.last().bytes.constData()];
            const Obj &x = resolve(entry);
            const int number = entry.type == Obj::Ref ? entry.ref : 0;
            if (x.isStream() && x["Subtype"].isName("Image")) {
                addPicture(number, x, ctm, page, seen);
            } else if (x.isStream() && x["Subtype"].isName("Form") && depth < 12 && !forms.contains(number)) {
                forms.insert(number);
                QTransform inner = ctm;
                const Obj &matrix = get(x, "Matrix");
                if (matrix.size() == 6) {
                    inner = QTransform(resolve(matrix.at(0)).num, resolve(matrix.at(1)).num, resolve(matrix.at(2)).num,
                                       resolve(matrix.at(3)).num, resolve(matrix.at(4)).num, resolve(matrix.at(5)).num)
                            * ctm;
                }
                const Obj &formResources = get(x, "Resources");
                walkContent(contentBytes(x), formResources.type == Obj::Dict ? formResources : resources, inner,
                            page, depth + 1, forms, seen);
                forms.remove(number);
            }
        } else if (op == "BI") {
            // Inline picture: skip its data up to "EI".
            while (!lx.atEnd()) {
                Obj t = lx.parse();
                if (t.isKeyword("ID") || t.type == Obj::Null) break;
            }
            qint64 p = lx.pos;
            while (p + 2 <= lx.n) {
                if (lx.s[p] == 'E' && lx.s[p + 1] == 'I' && p > 0 && isWhite(lx.s[p - 1])
                    && (p + 2 == lx.n || isWhite(lx.s[p + 2]) || isDelim(lx.s[p + 2]))) {
                    p += 2;
                    break;
                }
                ++p;
            }
            lx.pos = std::min(p, lx.n);
        }
        operands.clear();
    }
}

void PdfPictures::Impl::walkPages(const Obj &nodeIn, const Obj &resources, int depth, QSet<int> &visited, int *page) {
    if (depth > 64) return;
    if (nodeIn.type == Obj::Ref) {
        if (visited.contains(nodeIn.ref)) return;
        visited.insert(nodeIn.ref);
    }
    const Obj &node = resolve(nodeIn);
    if (node.type != Obj::Dict) return;
    const Obj &own = get(node, "Resources");
    const Obj &res = own.type == Obj::Dict ? own : resources;
    const Obj &kids = get(node, "Kids");
    if (kids.type == Obj::Array && !node["Type"].isName("Page")) {
        for (int i = 0; i < kids.size(); ++i) walkPages(kids.at(i), res, depth + 1, visited, page);
        return;
    }
    ++*page;
    QByteArray content;
    const Obj &contents = get(node, "Contents");
    if (contents.isStream()) {
        content = contentBytes(contents);
    } else if (contents.type == Obj::Array) {
        for (int i = 0; i < contents.size(); ++i) {
            content += contentBytes(resolve(contents.at(i)));
            content += '\n';
        }
    }
    QSet<int> forms;
    QSet<int> seen;
    for (const Picture &p : pictures) seen.insert(p.object);
    walkContent(content, res, QTransform(), *page, 0, forms, seen);
}

void PdfPictures::Impl::findPictures() {
    // The catalog: from the newest trailer or cross-reference stream.
    Obj root;
    for (const Obj &t : trailers)
        if (t["Root"].type == Obj::Ref) root = t["Root"];
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
        if (it->obj.isStream() && it->obj["Type"].isName("XRef") && it->obj["Root"].type == Obj::Ref)
            root = it->obj["Root"];
    if (resolve(root).type != Obj::Dict) {
        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
            if (it->obj["Type"].isName("Catalog")) { root = it->obj; break; }
    }
    QSet<int> visited;
    int page = 0;
    walkPages(get(resolve(root), "Pages"), Obj(), 0, visited, &page);
    if (!pictures.isEmpty()) return;

    // Nothing drawn was found (unreadable page contents): list every picture
    // in the file that is not just the transparency of another.
    QSet<int> masks;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        for (const char *key : {"SMask", "Mask"}) {
            const Obj &m = it->obj[key];
            if (m.type == Obj::Ref) masks.insert(m.ref);
        }
    }
    QList<int> numbers = objects.keys();
    std::sort(numbers.begin(), numbers.end());
    QSet<int> seen;
    for (int number : numbers) {
        const Obj &o = objects.value(number).obj;
        if (o.isStream() && o["Subtype"].isName("Image") && !masks.contains(number))
            addPicture(number, o, QTransform(), 0, seen);
    }
}

QImage PdfPictures::Impl::decodeGrayMask(const Obj &mask, const QSize &size, bool stencil, QString *problem) const {
    // A soft mask is a gray picture; a stencil mask is 1 bit where 1 means
    // "not painted" (both with the default Decode).
    QImage gray = decodeImage(mask, problem, 1);
    if (gray.isNull()) return gray;
    QImage alpha(gray.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < gray.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(gray.constScanLine(y));
        uchar *dst = alpha.scanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            // A stencil shows the picture where it would paint (sample 0).
            if (stencil) dst[x] = (qAlpha(src[x]) > 127 && qGray(src[x]) < 128) ? 255 : 0;
            else dst[x] = uchar(qGray(src[x]));
        }
    }
    if (alpha.size() != size) alpha = alpha.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return alpha;
}

QImage PdfPictures::Impl::decodeImage(const Obj &image, QString *problem, int depth) const {
    if (depth > 2 || !image.isStream()) { *problem = QStringLiteral("not a picture"); return QImage(); }
    const int w = get(image, "Width").toInt(0);
    const int h = get(image, "Height").toInt(0);
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535 || qint64(w) * h > 400000000LL) {
        *problem = QStringLiteral("The picture is too large or has no size.");
        return QImage();
    }
    QString compression;
    const QString pre = precheck(image, &compression);
    if (!pre.isEmpty()) { *problem = pre; return QImage(); }
    QByteArray bytes, codec;
    if (!streamBytes(image, &bytes, &codec, problem)) {
        if (problem->isEmpty()) *problem = QStringLiteral("The picture's data could not be read.");
        return QImage();
    }

    QImage result;
    const bool imageMask = get(image, "ImageMask").boolean;
    if (codec == "DCTDecode" || codec == "DCT") {
        result = QImage::fromData(bytes, "JPEG");
        if (result.isNull()) {
            *problem = QStringLiteral("The JPEG data of this picture could not be read.");
            return QImage();
        }
        // An inverted Decode array: CMYK JPEGs from Photoshop store inverted
        // values and say so in an "Adobe" marker; PDFs then carry
        // Decode [1 0 1 0 ...]. Qt's JPEG reader already undoes that
        // inversion (Qt 6.4 for every CMYK JPEG, newer Qt when the marker is
        // there), so it must not be applied twice. Gray and RGB JPEGs with an
        // inverted Decode are inverted here.
        const Obj &decode = get(image, "Decode");
        const bool inverted = decode.size() >= 2 && resolve(decode.at(0)).num > resolve(decode.at(1)).num;
        const bool cmyk = colorSpace(image["ColorSpace"]).kind == ColorSpace::CMYK;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (result.format() == QImage::Format_CMYK8888) {
            if (inverted && !hasAdobeMarker(bytes)) {
                for (int y = 0; y < result.height(); ++y) {
                    uchar *line = result.scanLine(y);
                    for (int x = 0; x < result.width() * 4; ++x) line[x] = uchar(255 - line[x]);
                }
            }
        } else
#endif
        if (inverted && !cmyk) {
            result.invertPixels();
        }
        result = result.convertToFormat(QImage::Format_ARGB32);
    } else {
        const ColorSpace cs = imageMask ? ColorSpace() : colorSpace(image["ColorSpace"]);
        const int bpc = imageMask ? 1 : get(image, "BitsPerComponent").toInt(8);
        if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16) {
            *problem = QStringLiteral("unsupported %1 bits per sample").arg(bpc);
            return QImage();
        }
        const int comps = cs.components;
        const int maxValue = (1 << std::min(bpc, 16)) - 1;
        // Decode: maps each sample to its range (default 0..1; indexed 0..2^bpc-1).
        double dmin[4], dmax[4];
        const Obj &decode = get(image, "Decode");
        for (int c = 0; c < comps && c < 4; ++c) {
            dmin[c] = 0;
            dmax[c] = cs.kind == ColorSpace::Indexed ? maxValue : 1;
            if (decode.size() >= 2 * (c + 1)) {
                dmin[c] = resolve(decode.at(2 * c)).toReal(dmin[c]);
                dmax[c] = resolve(decode.at(2 * c + 1)).toReal(dmax[c]);
            }
        }
        const qint64 rowBytes = (qint64(w) * comps * bpc + 7) / 8;
        if (bytes.size() < rowBytes * h) bytes.append(QByteArray(int(rowBytes * h - bytes.size()), '\0'));
        result = QImage(w, h, QImage::Format_ARGB32);
        if (result.isNull()) { *problem = QStringLiteral("not enough memory for the picture"); return QImage(); }
        const uchar *src = reinterpret_cast<const uchar *>(bytes.constData());
        // Color-key masking: raw sample ranges that are left transparent.
        const Obj &mask = get(image, "Mask");
        int keyRanges[8] = {0};
        const bool colorKey = mask.type == Obj::Array && mask.size() >= 2 * comps;
        if (colorKey)
            for (int i = 0; i < 2 * comps && i < 8; ++i) keyRanges[i] = resolve(mask.at(i)).toInt();
        for (int y = 0; y < h; ++y) {
            const uchar *row = src + y * rowBytes;
            QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
            qint64 bit = 0;
            for (int x = 0; x < w; ++x) {
                double v[4] = {0, 0, 0, 0};
                int raw[4] = {0, 0, 0, 0};
                for (int c = 0; c < comps && c < 4; ++c) {
                    int sample;
                    if (bpc == 8) sample = row[bit / 8];
                    else if (bpc == 16) sample = row[bit / 8];   // high byte
                    else sample = (row[bit / 8] >> (8 - bpc - bit % 8)) & maxValue;
                    raw[c] = bpc == 16 ? (row[bit / 8] << 8 | row[bit / 8 + 1]) : sample;
                    bit += bpc;
                    const double scale = bpc == 16 ? 255.0 : double(maxValue);
                    v[c] = dmin[c] + sample * (dmax[c] - dmin[c]) / scale;
                }
                if (imageMask) {
                    // Stencil: 0 paints (default Decode [0 1]).
                    out[x] = v[0] < 0.5 ? qRgba(0, 0, 0, 255) : qRgba(0, 0, 0, 0);
                    continue;
                }
                QRgb color = toRgb(cs, v);
                if (colorKey) {
                    bool inside = true;
                    for (int c = 0; c < comps && c < 4; ++c)
                        if (raw[c] < keyRanges[2 * c] || raw[c] > keyRanges[2 * c + 1]) { inside = false; break; }
                    if (inside) color &= 0x00ffffff;
                }
                out[x] = color;
            }
        }
    }

    // Transparency: a soft mask, or a stencil mask stream.
    if (depth == 0) {
        const Obj &smask = get(image, "SMask");
        const Obj &mask = get(image, "Mask");
        QImage alpha;
        QString maskProblem;
        if (smask.isStream()) alpha = decodeGrayMask(smask, result.size(), false, &maskProblem);
        else if (mask.isStream()) alpha = decodeGrayMask(mask, result.size(), true, &maskProblem);
        if (!alpha.isNull()) {
            for (int y = 0; y < result.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(result.scanLine(y));
                const uchar *a = alpha.constScanLine(y);
                for (int x = 0; x < result.width(); ++x)
                    line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), qAlpha(line[x]) * a[x] / 255);
            }
        }
    }
    return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// ===================================================================== public

PdfPictures::PdfPictures() : d(std::make_unique<Impl>()) {}
PdfPictures::~PdfPictures() = default;

bool PdfPictures::open(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        d->error = QStringLiteral("The file could not be opened.");
        return false;
    }
    if (file.size() > qint64(2000) * 1024 * 1024) {
        d->error = QStringLiteral("The file is too large (over 2 GB).");
        return false;
    }
    return openData(file.readAll());
}

bool PdfPictures::openData(const QByteArray &data) {
    d = std::make_unique<Impl>();
    d->data = data;
    if (!data.left(1024).contains("%PDF")) {
        d->error = QStringLiteral("This is not a PDF file.");
        return false;
    }
    d->scan();
    d->expandObjectStreams();
    bool encrypted = false;
    for (const Obj &t : d->trailers)
        if (t["Encrypt"].type != Obj::Null) encrypted = true;
    for (auto it = d->objects.constBegin(); it != d->objects.constEnd(); ++it)
        if (it->obj["Type"].isName("XRef") && it->obj["Encrypt"].type != Obj::Null) encrypted = true;
    if (encrypted) {
        d->error = QStringLiteral("This PDF is protected (encrypted), so its pictures cannot be read.");
        return false;
    }
    if (d->objects.isEmpty()) {
        d->error = QStringLiteral("The PDF could not be read; it may be damaged.");
        return false;
    }
    d->findPictures();
    return true;
}

QString PdfPictures::error() const { return d->error; }

const QVector<PdfPictures::Picture> &PdfPictures::pictures() const { return d->pictures; }

QImage PdfPictures::decode(int index, QString *problem) const {
    QString why;
    QImage image;
    if (index >= 0 && index < d->pictures.size()) {
        const Picture &p = d->pictures.at(index);
        if (!p.problem.isEmpty()) why = p.problem;
        else image = d->decodeImage(d->objects.value(p.object).obj, &why);
    } else {
        why = QStringLiteral("There is no such picture.");
    }
    if (problem) *problem = why;
    return image;
}

double PdfPictures::suggestedDpi(const Picture &picture) {
    double dpi = picture.dpiX > 0 && picture.dpiY > 0 ? (picture.dpiX + picture.dpiY) / 2.0 : picture.dpiX;
    if (dpi <= 0 || !std::isfinite(dpi)) return 0;
    if (std::abs(dpi - std::round(dpi)) < 0.05) dpi = std::round(dpi);
    return std::round(dpi * 100.0) / 100.0;
}
