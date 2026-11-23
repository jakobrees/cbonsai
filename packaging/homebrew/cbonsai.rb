# Lives in the tap repo (jakobrees/homebrew-tap) as Formula/cbonsai.rb:
#   brew install jakobrees/tap/cbonsai
# Replace sha256 after tagging:
#   curl -sL <url> | shasum -a 256
class Cbonsai < Formula
  desc "Grow bonsai trees in your terminal, with seasonal color and real-time growth"
  homepage "https://github.com/jakobrees/cbonsai"
  url "https://github.com/jakobrees/cbonsai/archive/refs/tags/v2.0.0.tar.gz"
  sha256 "REPLACE_AFTER_TAGGING"
  license "GPL-3.0-only"
  head "https://github.com/jakobrees/cbonsai.git", branch: "master"

  depends_on "pkgconf" => :build
  depends_on "scdoc" => :build
  depends_on "ncurses"

  def install
    system "make", "install", "PREFIX=#{prefix}", "MANDIR=#{man}"
  end

  test do
    assert_match "cbonsai", shell_output("#{bin}/cbonsai --version")
  end
end
