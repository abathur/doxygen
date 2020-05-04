{ pkgs ? import <nixpkgs> {} }:

pkgs.doxygen.overrideAttrs (attrs: {
  name = "doxygen-sqlite3again";
  src = ./.;
  buildInputs = attrs.buildInputs ++ [ pkgs.sqlite ];
  cmakeFlags = [
    "-Duse_sqlite3=ON"
  ] ++ attrs.cmakeFlags;

  doCheck = true;
  checkInputs = [ pkgs.time ];
  checkPhase = ''
    # rm outputs; not necessary with nix-build but useful with nix-shell
    rm -rf xml html latex doxygen_sqlite3.db

    # build process changes directory to doxygen/build
    cd ../

    # for STRIP_FROM_PATH
    DOXYGEN_ROOT=$(pwd)

    # make the examples
    command time --verbose build/bin/doxygen examples.conf

    # show the XML
    head -n -0 xml/index.xml xml/group__group5_*.xml

    # show the sql
    sqlite3 doxygen_sqlite3.db -column -header ".width 5 5 0 40" "select rowid, kind, refid, name from def"

    # test whether name is right in sqlite3 db
    sqlite3 doxygen_sqlite3.db "select name from def where kind='page' and name='mypage1'" | grep mypage1

    # cd back into build so that 'make install' will work and not muddy the water
    cd build
  '';
})
