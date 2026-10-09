# Login shells source this from /etc/profile. Root logs in to bash (post-build.sh);
# the stock profile's PS1 ('# ') names neither the unit nor the directory.
if [ -n "$BASH_VERSION" ]; then
    PS1='\u@\h:\w\$ '
    alias ll='ls -l'
    alias la='ls -la'
    [ -f "$HOME/.bashrc" ] && . "$HOME/.bashrc"
fi
