{ pkgs ? import <nixpkgs> {} }:

pkgs.doxygen.overrideAttrs (attrs: {
  name = "doxygen-79ace061";
  src = ./.;
  buildInputs = attrs.buildInputs ++ [ pkgs.sqlite ];
  cmakeFlags = [
    "-Duse_sqlite3=ON"
  ] ++ attrs.cmakeFlags;

  doCheck = true;
  checkPhase = ''
    # rm outputs; not necessary with nix-build but useful with nix-shell
    rm -rf html latex doxygen_sqlite3.db
    # build process changes directory to doxygen/build
    cd ../
    build/bin/doxygen examples.conf
    # show that the name is right in XML
    head -n 20 xml/group__group5_mypage1.xml
    # test whether it's right in sqlite3 db
    sqlite3 doxygen_sqlite3.db "select name from def where kind='page' and name='mypage1'" | grep mypage1

    # cd back into build so that 'make install' will work and not muddy the water
    cd build
  '';
})
